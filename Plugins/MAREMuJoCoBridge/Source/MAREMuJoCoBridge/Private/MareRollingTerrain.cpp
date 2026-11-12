#include "MareRollingTerrain.h"

#include "EngineUtils.h"
#include "MuJoCo/Components/Bodies/MjBody.h"
#include "MuJoCo/Components/QuickConvert/AMjHeightfieldActor.h"
#include "MuJoCo/Core/AMjManager.h"
#include "MuJoCo/Core/MjArticulation.h"
#include "MuJoCo/Core/MjPhysicsEngine.h"
#include "mujoco/mujoco.h"

DEFINE_LOG_CATEGORY_STATIC(LogMareTerrain, Log, All);

/** State shared with the physics-thread callback; reference-counted so it may outlive the actor. */
struct FMareRollingTerrainShared
{
	FCriticalSection Lock;
	std::atomic<bool> bAlive{true};
	std::atomic<bool> bPending{false};
	int32 HField = -1, Geom = -1;
	TArray<float> Data;            // normalised heights, MuJoCo row-major (rows along +y)
	double Pos[3] = {0, 0, 0};     // geom position (m, MuJoCo frame)
	double Size2 = 1.0, Size3 = 0.1;   // hfield elevation range and base thickness (m)
};

AMareRollingTerrain::AMareRollingTerrain()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void AMareRollingTerrain::BeginPlay()
{
	Super::BeginPlay();
	if (!Rover)
		for (TActorIterator<AMjArticulation> It(GetWorld()); It; ++It)
		{
			Rover = *It;
			break;
		}
}

void AMareRollingTerrain::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Shared)
		Shared->bAlive = false;
	Super::EndPlay(Reason);
}

bool AMareRollingTerrain::Initialise()
{
	AAMjManager* Manager = nullptr;
	for (TActorIterator<AAMjManager> It(GetWorld()); It; ++It)
	{
		Manager = *It;
		break;
	}
	if (!Manager || !Manager->IsInitialized() || !Manager->PhysicsEngine || !Heightfield || !Rover)
		return false;
	for (UMjBody* B : Rover->GetBodies())
		if (B && (B->GetName() == FollowBody || B->GetMjName().EndsWith(FollowBody)))
			Body = B;
	mjModel* M = Manager->PhysicsEngine->GetModel();
	if (!Body.IsValid() || !M)
		return false;

	Shared = MakeShared<FMareRollingTerrainShared, ESPMode::ThreadSafe>();
	Shared->HField = mj_name2id(M, mjOBJ_HFIELD, TCHAR_TO_UTF8(*Heightfield->HFieldName));
	for (int g = 0; g < M->ngeom && Shared->HField >= 0; ++g)
		if (M->geom_type[g] == mjGEOM_HFIELD && M->geom_dataid[g] == Shared->HField)
			Shared->Geom = g;
	if (Shared->HField < 0 || Shared->Geom < 0)
	{
		UE_LOG(LogMareTerrain, Error, TEXT("Heightfield '%s' not found in the compiled model"), *Heightfield->HFieldName);
		bReady = true;   // give up quietly
		return false;
	}
	const int H = Shared->HField, G = Shared->Geom;
	const int NRow = M->hfield_nrow[H], NCol = M->hfield_ncol[H];
	if (NRow != NCol)
	{
		UE_LOG(LogMareTerrain, Error, TEXT("Rolling terrain needs a square heightfield (%dx%d)"), NRow, NCol);
		bReady = true;
		return false;
	}
	// Recover the compiled patch (centre, extent, heights) in Unreal units.
	N = NRow;
	const double HalfX = M->hfield_size[4 * H] * 100.0, HalfY = M->hfield_size[4 * H + 1] * 100.0;
	Step = 2.0 * HalfX / (N - 1);
	const double Cx = M->geom_pos[3 * G] * 100.0, Cy = -M->geom_pos[3 * G + 1] * 100.0, Bz = M->geom_pos[3 * G + 2] * 100.0;
	OriginX = Cx - HalfX;
	OriginY = Cy - HalfY;
	Ci = Cj = 0;
	Heights.SetNumUninitialized(N * N);
	const float* Src = M->hfield_data + M->hfield_adr[H];
	const double Range = M->hfield_size[4 * H + 2] * 100.0;
	for (int r = 0; r < N; ++r)          // MuJoCo row r <=> Unreal Y = MaxY - r * Step
		for (int c = 0; c < N; ++c)
			Heights[(N - 1 - r) * N + c] = float(Bz + Src[r * N + c] * Range);

	FBox Box = Heightfield->BoundsBox->CalcBounds(Heightfield->BoundsBox->GetComponentTransform()).GetBox();
	TraceTopZ = Box.Max.Z;
	TraceBottomZ = Box.Min.Z;
	for (const TSoftObjectPtr<AActor>& A : Heightfield->TraceWhitelist)
		if (AActor* Actor = A.Get())
			Whitelist.Add(Actor);
	Shared->Size3 = M->hfield_size[4 * H + 3];

	// Applies a pending patch before the next step (physics thread, under URLab's callback lock).
	TSharedPtr<FMareRollingTerrainShared, ESPMode::ThreadSafe> S = Shared;
	Manager->PhysicsEngine->RegisterPreStepCallback([S](mjModel* m, mjData* /*d*/)
	{
		if (!S->bAlive || !S->bPending)
			return;
		FScopeLock Guard(&S->Lock);
		const int h = S->HField, g = S->Geom;
		FMemory::Memcpy(m->hfield_data + m->hfield_adr[h], S->Data.GetData(), S->Data.Num() * sizeof(float));
		m->hfield_size[4 * h + 2] = S->Size2;
		m->hfield_size[4 * h + 3] = S->Size3;
		for (int k = 0; k < 3; ++k)
			m->geom_pos[3 * g + k] = S->Pos[k];
		// Keep the broad-phase bounds consistent with the new elevation range.
		const double hx = m->hfield_size[4 * h], hy = m->hfield_size[4 * h + 1];
		const double cz = 0.5 * (S->Size2 - S->Size3), hz = 0.5 * (S->Size2 + S->Size3);
		mjtNum* aabb = m->geom_aabb + 6 * g;
		aabb[0] = 0; aabb[1] = 0; aabb[2] = cz; aabb[3] = hx; aabb[4] = hy; aabb[5] = hz;
		m->geom_rbound[g] = FMath::Sqrt(hx * hx + hy * hy + FMath::Square(FMath::Abs(cz) + hz));
		S->bPending = false;
	});

	Publish();   // re-encode the compiled patch with the fixed elevation range
	UE_LOG(LogMareTerrain, Log, TEXT("Rolling terrain on '%s': %dx%d samples, %.1f cm apart, %.0f m range; moves after %.0f m"),
		*Heightfield->HFieldName, N, N, Step, ElevationRange, RecentreDistance);
	bReady = true;
	return true;
}

float AMareRollingTerrain::SampleHeight(double X, double Y) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MareRollingTerrain), true);
	Params.AddIgnoredActor(Heightfield);
	if (Rover)
		Params.AddIgnoredActor(Rover);
	FHitResult Hit;
	FVector Start(X, Y, TraceTopZ);
	const FVector End(X, Y, TraceBottomZ);
	for (int Tries = 0; Tries < 8 && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, Heightfield->ElevationTraceChannel, Params); ++Tries)
	{
		AActor* A = Hit.GetActor();
		if (!A || Whitelist.Num() == 0 || Whitelist.Contains(A))
			return float(Hit.ImpactPoint.Z);
		Params.AddIgnoredActor(A);         // not terrain: continue below it
		Start = Hit.ImpactPoint - FVector(0, 0, 1.0);
	}
	return float(TraceBottomZ);
}

void AMareRollingTerrain::StartRecentre(int32 NewCi, int32 NewCj)
{
	NextCi = NewCi;
	NextCj = NewCj;
	NextHeights.SetNumUninitialized(N * N);
	ToSample.Reset();
	const int32 Di = NewCi - Ci, Dj = NewCj - Cj;
	for (int32 j = 0; j < N; ++j)
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Oi = i + Di, Oj = j + Dj;      // same lattice cell in the current patch
			if (Oi >= 0 && Oi < N && Oj >= 0 && Oj < N)
				NextHeights[j * N + i] = Heights[Oj * N + Oi];
			else
				ToSample.Add(j * N + i);
		}
	SampleCursor = 0;
	bSampling = true;
}

void AMareRollingTerrain::ContinueSampling()
{
	const int32 End = FMath::Min(SampleCursor + TracesPerFrame, ToSample.Num());
	for (; SampleCursor < End; ++SampleCursor)
	{
		const int32 k = ToSample[SampleCursor], i = k % N, j = k / N;
		NextHeights[k] = SampleHeight(OriginX + (NextCi + i) * Step, OriginY + (NextCj + j) * Step);
	}
	if (SampleCursor < ToSample.Num())
		return;
	Ci = NextCi;
	Cj = NextCj;
	Swap(Heights, NextHeights);
	bSampling = false;
	++Recentres;
	Publish();
	UE_LOG(LogMareTerrain, Log, TEXT("Terrain patch moved (%d): centre (%.1f, %.1f) m in the MuJoCo frame, %d new samples"),
		Recentres, (OriginX + (Ci + (N - 1) * 0.5) * Step) / 100.0, -(OriginY + (Cj + (N - 1) * 0.5) * Step) / 100.0, ToSample.Num());
}

void AMareRollingTerrain::Publish()
{
	float MinZ = FLT_MAX;
	for (float Z : Heights)
		MinZ = FMath::Min(MinZ, Z);
	const double Base = MinZ - 50.0;                 // 0.5 m margin below the lowest sample
	const double Range = ElevationRange * 100.0;
	FScopeLock Guard(&Shared->Lock);
	Shared->Data.SetNumUninitialized(N * N);
	for (int r = 0; r < N; ++r)
		for (int c = 0; c < N; ++c)
			Shared->Data[r * N + c] = float(FMath::Clamp((Heights[(N - 1 - r) * N + c] - Base) / Range, 0.0, 1.0));
	const double Half = (N - 1) * 0.5 * Step;
	Shared->Pos[0] = (OriginX + Ci * Step + Half) / 100.0;
	Shared->Pos[1] = -(OriginY + Cj * Step + Half) / 100.0;
	Shared->Pos[2] = Base / 100.0;
	Shared->Size2 = ElevationRange;
	Shared->bPending = true;
}

void AMareRollingTerrain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady && !Initialise())
		return;
	if (!Shared || !Body.IsValid())
		return;
	if (bSampling)
	{
		ContinueSampling();
		return;
	}
	// Recentre when the rover leaves the central region.
	const FVector P = Body->GetComponentLocation();
	const double Half = (N - 1) * 0.5 * Step;
	const double Cx = OriginX + Ci * Step + Half, Cy = OriginY + Cj * Step + Half;
	const double Limit = RecentreDistance * 100.0;
	if (FMath::Abs(P.X - Cx) > Limit || FMath::Abs(P.Y - Cy) > Limit)
		StartRecentre(FMath::RoundToInt((P.X - Half - OriginX) / Step), FMath::RoundToInt((P.Y - Half - OriginY) / Step));
}
