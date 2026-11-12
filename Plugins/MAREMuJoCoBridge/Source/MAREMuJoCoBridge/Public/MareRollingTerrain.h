#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MareRollingTerrain.generated.h"

class AMjHeightfieldActor;
class AMjArticulation;
struct FMareRollingTerrainShared;

/**
 * Keeps a URLab heightfield centred on the rover so that MuJoCo has high-resolution terrain anywhere on the map.
 *
 * The heightfield actor provides the initial patch. When the rover moves more than RecentreDistance from the
 * patch centre, the patch is shifted on the same sample lattice: overlapping samples are reused, the uncovered
 * strip is traced over several frames, and the result is written into the compiled model between physics steps.
 */
UCLASS(meta = (DisplayName = "MARE Rolling Terrain"))
class MAREMUJOCOBRIDGE_API AMareRollingTerrain : public AActor
{
	GENERATED_BODY()

public:
	AMareRollingTerrain();

	/** Heightfield to move; its size and resolution are kept. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Terrain")
	TObjectPtr<AMjHeightfieldActor> Heightfield;

	/** Rover to follow. If unset, the first AMjArticulation in the level. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Terrain")
	TObjectPtr<AMjArticulation> Rover;

	/** Body whose position is followed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FString FollowBody = TEXT("base_link");

	/** Distance from the patch centre (m, along X or Y) that triggers a recentre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1.0"))
	float RecentreDistance = 8.0f;

	/** Elevation range of the heightfield (m); must exceed the relief within one patch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1.0"))
	float ElevationRange = 30.0f;

	/** Line traces per frame while sampling a new strip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "100"))
	int32 TracesPerFrame = 20000;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Terrain")
	int32 Recentres = 0;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool Initialise();
	void StartRecentre(int32 NewCi, int32 NewCj);
	void ContinueSampling();
	float SampleHeight(double X, double Y) const;
	void Publish();

	TSharedPtr<FMareRollingTerrainShared, ESPMode::ThreadSafe> Shared;
	TWeakObjectPtr<class UMjBody> Body;
	bool bReady = false;

	// Sample lattice: cell (i, j) is at (OriginX + i * Step, OriginY + j * Step) cm in Unreal world space.
	int32 N = 0;
	double Step = 0.0, OriginX = 0.0, OriginY = 0.0;
	int32 Ci = 0, Cj = 0;                 // lattice index of the patch's min-X, min-Y cell
	TArray<float> Heights;                // N x N heights (cm), index j * N + i
	double TraceTopZ = 0.0, TraceBottomZ = 0.0;
	TSet<AActor*> Whitelist;

	// Recentre in progress.
	bool bSampling = false;
	int32 NextCi = 0, NextCj = 0;
	TArray<float> NextHeights;
	TArray<int32> ToSample;               // indices into NextHeights still to sample
	int32 SampleCursor = 0;
};
