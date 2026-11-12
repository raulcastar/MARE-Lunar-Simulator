#include "MareMuJoCoRoverBridge.h"

#include "Common/TcpSocketBuilder.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GncSimTimeLibrary.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "MuJoCo/Components/Actuators/MjActuator.h"
#include "MuJoCo/Components/Bodies/MjBody.h"
#include "MuJoCo/Components/MjComponent.h"
#include "MuJoCo/Core/AMjManager.h"
#include "MuJoCo/Core/MjArticulation.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMareBridge, Log, All);

namespace
{
	/** True if MjName equals Wanted or ends with it after a URLab articulation prefix. */
	bool NameMatches(const FString& MjName, const FString& Wanted)
	{
		return MjName == Wanted || MjName.EndsWith(TEXT("/") + Wanted) || MjName.EndsWith(TEXT("_") + Wanted);
	}

	/** Matches the MuJoCo name, or the component name for components that leave MjName empty (e.g. actuators). */
	bool ComponentMatches(const UMjComponent* Comp, const FString& Wanted)
	{
		return NameMatches(Comp->GetMjName(), Wanted) || Comp->GetName() == Wanted;
	}
}

AMareMuJoCoRoverBridge::AMareMuJoCoRoverBridge()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;   // step before the manager applies the render state
	WheelActuators.Add(TEXT("FL"), TEXT("wheel_fl"));
	WheelActuators.Add(TEXT("BL"), TEXT("wheel_rl"));
	WheelActuators.Add(TEXT("FR"), TEXT("wheel_fr"));
	WheelActuators.Add(TEXT("BR"), TEXT("wheel_rr"));
	SteerActuators.Add(TEXT("SFL"), TEXT("steer_fl"));
	SteerActuators.Add(TEXT("SBL"), TEXT("steer_rl"));
	SteerActuators.Add(TEXT("SFR"), TEXT("steer_fr"));
	SteerActuators.Add(TEXT("SBR"), TEXT("steer_rr"));
}

void AMareMuJoCoRoverBridge::BeginPlay()
{
	Super::BeginPlay();
	if (!Rover)
	{
		for (TActorIterator<AMjArticulation> It(GetWorld()); It; ++It)
		{
			Rover = *It;
			break;
		}
	}
	if (Rover)
	{
		Rover->ControlSource = 1;   // 1 = UI source: SetControl() values are applied (0 = ZMQ)
		UE_LOG(LogMareBridge, Log, TEXT("Driving %s; dialling %s:%d"), *Rover->GetName(), *Host, Port);
	}
	else
	{
		UE_LOG(LogMareBridge, Warning, TEXT("No AMjArticulation in the level: nothing to drive"));
	}
	PrepareSensorRig();
}

void AMareMuJoCoRoverBridge::PrepareSensorRig()
{
	if (!SensorRig)
		return;
	// Keep the rig's sensors and lights; disable physics, collision and its own TCP link.
	TArray<UActorComponent*> Comps;
	SensorRig->GetComponents(Comps);
	for (UActorComponent* C : Comps)
	{
		if (UPhysicsConstraintComponent* Con = Cast<UPhysicsConstraintComponent>(C))
		{
			Con->BreakConstraint();
		}
		else if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
		{
			Prim->SetSimulatePhysics(false);
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (Prim->IsA<UStaticMeshComponent>() && !Prim->IsEditorOnly())
			{
				Prim->SetVisibility(bUseRigVisuals);
				Prim->SetHiddenInGame(!bUseRigVisuals);
				Prim->SetCastShadow(bUseRigVisuals);
			}
		}
		else if (USpotLightComponent* Light = Cast<USpotLightComponent>(C))
		{
			HeadlightMax.Add(Light, Light->Intensity);
		}
		else if (C->GetClass()->GetName().Contains(TEXT("TCPClient")))
		{
			if (UFunction* Close = C->FindFunction(TEXT("CloseSocket")))
				C->ProcessEvent(Close, nullptr);
			C->Deactivate();
			C->SetComponentTickEnabled(false);
		}
	}
	// Use the Blueprint's MaxHeadlightIntensity as full brightness when it defines one.
	if (const FProperty* P = SensorRig->GetClass()->FindPropertyByName(TEXT("MaxHeadlightIntensity")))
	{
		double Max = 0.0;
		if (const FDoubleProperty* D = CastField<FDoubleProperty>(P))
			Max = D->GetPropertyValue_InContainer(SensorRig);
		else if (const FFloatProperty* F = CastField<FFloatProperty>(P))
			Max = F->GetPropertyValue_InContainer(SensorRig);
		if (Max > 0.0)
			for (TPair<TWeakObjectPtr<USpotLightComponent>, float>& L : HeadlightMax)
				L.Value = Max;
	}
	SetHeadlights(false, 0.0);

	if (bUseRigVisuals && Rover)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Rover->GetComponents(Meshes);
		for (UStaticMeshComponent* M : Meshes)
			if (M->GetName().StartsWith(TEXT("Viz_")))
				M->SetVisibility(false);   // MJCF visual meshes
	}
}

void AMareMuJoCoRoverBridge::BindRigWheels()
{
	if (bRigWheelsBound || !bUseRigVisuals || !SensorRig || !Rover || !BaseBody || !bRigAttached)
		return;
	bRigWheelsBound = true;
	// MuJoCo wheel bodies (wheel_*_link) and their positions in the base frame; a steering body
	// steer_*_link of the same wheel adds its joint.
	TArray<TPair<FString, FVector>> MjWheels;
	TSet<FString> SteerBodies;
	const FTransform Base = BaseBody->GetComponentTransform();
	for (UMjBody* Body : Rover->GetBodies())
	{
		const FString Name = Body ? Body->GetName() : FString();
		if (Name.StartsWith(TEXT("wheel_")) && Name.EndsWith(TEXT("_link")))
			MjWheels.Add({Name.LeftChop(5) + TEXT("_joint"), Base.InverseTransformPosition(Body->GetComponentLocation())});
		else if (Name.StartsWith(TEXT("steer_")) && Name.EndsWith(TEXT("_link")))
			SteerBodies.Add(Name);
	}
	// Pair each rig wheel mesh with the nearest MuJoCo wheel (Blueprint component names are not reliable).
	TArray<UStaticMeshComponent*> Meshes;
	SensorRig->GetComponents(Meshes);
	for (UStaticMeshComponent* M : Meshes)
	{
		if (!M->GetName().EndsWith(TEXT("_Wheel")) || !M->GetAttachParent())
			continue;
		const FVector P = Base.InverseTransformPosition(M->Bounds.Origin);
		const TPair<FString, FVector>* Best = nullptr;
		for (const TPair<FString, FVector>& W : MjWheels)
			if (!Best || FVector::Dist2D(W.Value, P) < FVector::Dist2D(Best->Value, P))
				Best = &W;
		if (!Best || FVector::Dist2D(Best->Value, P) > 8.0)
			continue;
		FRigWheel R;
		R.Mesh = M;
		R.Joint = Best->Key;
		R.Rest = M->GetRelativeRotation().Quaternion();
		// A MuJoCo rotation about +y is an Unreal rotation about +Y by the same angle (see URLabAxisConv).
		const FTransform Parent = M->GetAttachParent()->GetComponentTransform();
		R.AxleInParent = Parent.InverseTransformVectorNoScale(Base.GetUnitAxis(EAxis::Y));
		R.SteerAxisInParent = Parent.InverseTransformVectorNoScale(Base.GetUnitAxis(EAxis::Z));
		const FString Wheel = Best->Key.Mid(6).LeftChop(6);   // wheel_fl_joint -> fl
		if (SteerBodies.Contains(TEXT("steer_") + Wheel + TEXT("_link")))
			R.SteerJoint = TEXT("steer_") + Wheel + TEXT("_joint");
		RigWheels.Add(R);
	}
	UE_LOG(LogMareBridge, Log, TEXT("Rig visuals: %d wheels follow the MuJoCo wheel joints (%d steered)"),
		RigWheels.Num(), SteerBodies.Num());
}

void AMareMuJoCoRoverBridge::SpinRigWheels()
{
	// Spin about the axle, then steer about the base's up axis. A MuJoCo rotation about +z is an
	// Unreal rotation about +Z by the opposite angle (the handedness flip, see URLabAxisConv).
	for (const FRigWheel& W : RigWheels)
		if (UStaticMeshComponent* M = W.Mesh.Get())
		{
			const FQuat Spin(W.AxleInParent, Rover->GetJointAngle(W.Joint));
			const FQuat Steer = W.SteerJoint.IsEmpty() ? FQuat::Identity
				: FQuat(W.SteerAxisInParent, -Rover->GetJointAngle(W.SteerJoint));
			M->SetRelativeRotation(Steer * Spin * W.Rest);
		}
}

void AMareMuJoCoRoverBridge::AttachSensorRig()
{
	if (bRigAttached || !SensorRig || !BaseBody || !bSimulationStarted)
		return;
	USceneComponent* Anchor = nullptr;
	TArray<USceneComponent*> Scene;
	SensorRig->GetComponents(Scene);
	for (USceneComponent* C : Scene)
		if (C->GetFName() == SensorRigAnchor)
			Anchor = C;
	USceneComponent* Root = SensorRig->GetRootComponent();
	if (!Anchor || !Root)
	{
		UE_LOG(LogMareBridge, Warning, TEXT("Sensor rig %s has no component '%s'"), *SensorRig->GetName(), *SensorRigAnchor.ToString());
		bRigAttached = true;   // do not retry
		return;
	}
	// Place the rig so that its anchor coincides with the base body, then attach it.
	const FTransform AnchorInRoot = Anchor->GetComponentTransform().GetRelativeTransform(Root->GetComponentTransform());
	SensorRig->SetActorTransform(AnchorInRoot.Inverse() * BaseBody->GetComponentTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	SensorRig->AttachToComponent(BaseBody, FAttachmentTransformRules::KeepWorldTransform);
	bRigAttached = true;
	UE_LOG(LogMareBridge, Log, TEXT("Sensor rig %s attached to %s"), *SensorRig->GetName(), *BaseBody->GetName());
}

void AMareMuJoCoRoverBridge::StartSimulation()
{
	if (!bStartSimulationOnPlay || bSimulationStarted)
		return;
	for (TActorIterator<AAMjManager> It(GetWorld()); It; ++It)
	{
		if (It->IsInitialized())
		{
			Manager = *It;
			if (bStepWithGameTime)
			{
				It->SetPaused(true);                  // URLab's loop keeps applying controls; this actor steps
				It->AddTickPrerequisiteActor(this);   // the manager renders the state stepped this frame
			}
			else
			{
				It->SetPaused(false);
			}
			bSimulationStarted = true;
			UE_LOG(LogMareBridge, Log, TEXT("MuJoCo simulation started (%s)"),
				bStepWithGameTime ? TEXT("stepped with game time") : TEXT("URLab real-time loop"));
		}
		break;
	}
}

void AMareMuJoCoRoverBridge::StepPhysics(float DeltaSeconds)
{
	AAMjManager* M = Manager.Get();
	if (!bStepWithGameTime || !bSimulationStarted || !M || !M->IsInitialized())
		return;
	if (M->IsRunning())
	{
		StepAccum = 0.0;     // URLab's real-time loop is stepping
		return;
	}
	const double Dt = FMath::Max(1e-5, double(M->GetTimestep()));
	StepAccum += DeltaSeconds;
	int32 Steps = FMath::FloorToInt(StepAccum / Dt);
	if (Steps <= 0)
		return;
	StepAccum -= Steps * Dt;
	if (Steps > MaxStepsPerFrame)
	{
		Steps = MaxStepsPerFrame;
		StepAccum = 0.0;
	}
	M->StepSync(Steps);
}

void AMareMuJoCoRoverBridge::SetHeadlights(bool bOn, double Level)
{
	for (TPair<TWeakObjectPtr<USpotLightComponent>, float>& L : HeadlightMax)
	{
		if (USpotLightComponent* Light = L.Key.Get())
		{
			const bool bLit = bOn && Level > 1e-6;
			Light->SetVisibility(bLit);
			Light->SetIntensity(bLit ? float(L.Value * FMath::Clamp(Level, 0.0, 1.0)) : 0.f);
		}
	}
}

void AMareMuJoCoRoverBridge::EndPlay(const EEndPlayReason::Type Reason)
{
	Disconnect(TEXT("end of play"));
	Super::EndPlay(Reason);
}

bool AMareMuJoCoRoverBridge::Bind()
{
	if (!Rover)
		return false;
	if (BaseBody && Actuators.Num() == WheelActuators.Num())
		return true;

	// Retried each tick until the manager has compiled the model.
	BaseBody = nullptr;
	for (UMjBody* Body : Rover->GetBodies())
	{
		if (Body && ComponentMatches(Body, BaseBodyName))
		{
			BaseBody = Body;
			break;
		}
	}
	Actuators.Reset();
	const TArray<UMjActuator*> All = Rover->GetActuators();
	for (const TPair<FString, FString>& Wheel : WheelActuators)
	{
		for (UMjActuator* Act : All)
		{
			if (Act && ComponentMatches(Act, Wheel.Value))
			{
				Actuators.Add(Wheel.Key, Act);
				break;
			}
		}
	}
	// Steering is optional: missing actuators leave those wheels fixed.
	SteerActs.Reset();
	for (const TPair<FString, FString>& Steer : SteerActuators)
		for (UMjActuator* Act : All)
			if (Act && ComponentMatches(Act, Steer.Value))
			{
				SteerActs.Add(Steer.Key, Act);
				break;
			}
	const bool bOk = BaseBody && Actuators.Num() == WheelActuators.Num();
	if (bOk)
		UE_LOG(LogMareBridge, Log, TEXT("Bound %d wheel and %d steering actuators"), Actuators.Num(), SteerActs.Num());
	if (!bOk && !bWarnedBind && GetWorld()->GetTimeSeconds() > 3.0)
	{
		bWarnedBind = true;
		TArray<FString> Names;
		for (UMjActuator* Act : All)
			if (Act) Names.Add(Act->GetName() + TEXT("/") + Act->GetMjName());
		UE_LOG(LogMareBridge, Warning, TEXT("Base body '%s' %s; %d/%d wheel actuators found (articulation has: %s)"),
			*BaseBodyName, BaseBody ? TEXT("found") : TEXT("NOT found"), Actuators.Num(), WheelActuators.Num(),
			*FString::Join(Names, TEXT(", ")));
	}
	return bOk;
}

void AMareMuJoCoRoverBridge::SetWheelSpeeds(float FL, float BL, float FR, float BR)
{
	if (!Bind())
		return;
	const TPair<const TCHAR*, float> Speeds[] = {{TEXT("FL"), FL}, {TEXT("BL"), BL}, {TEXT("FR"), FR}, {TEXT("BR"), BR}};
	for (const auto& S : Speeds)
		if (TObjectPtr<UMjActuator>* Act = Actuators.Find(S.Key))
			(*Act)->SetControl(S.Value);
}

void AMareMuJoCoRoverBridge::SetSteeringAngles(float FL, float BL, float FR, float BR)
{
	if (!Bind())
		return;
	const TPair<const TCHAR*, float> Angles[] = {{TEXT("SFL"), FL}, {TEXT("SBL"), BL}, {TEXT("SFR"), FR}, {TEXT("SBR"), BR}};
	for (const auto& A : Angles)
		if (TObjectPtr<UMjActuator>* Act = SteerActs.Find(A.Key))
			(*Act)->SetControl(A.Value);
}

void AMareMuJoCoRoverBridge::Connect()
{
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	FIPv4Address Ip;
	if (!FIPv4Address::Parse(Host, Ip))
	{
		UE_LOG(LogMareBridge, Error, TEXT("Bad host address '%s'"), *Host);
		NextConnectTime = FPlatformTime::Seconds() + 10.0;
		return;
	}
	Socket = FTcpSocketBuilder(TEXT("MareMuJoCoRoverBridge")).AsNonBlocking().WithReceiveBufferSize(1 << 16).Build();
	if (!Socket)
		return;
	Socket->SetNoDelay(true);
	TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr();
	Addr->SetIp(Ip.Value);
	Addr->SetPort(Port);
	Socket->Connect(*Addr);   // non-blocking; the result is checked in Tick
	ConnectStartTime = FPlatformTime::Seconds();
}

void AMareMuJoCoRoverBridge::Disconnect(const TCHAR* Why)
{
	if (Socket)
	{
		if (bConnected)
			UE_LOG(LogMareBridge, Log, TEXT("Disconnected from %s:%d (%s)"), *Host, Port, Why);
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	bConnected = false;
	Inbox.Reset();
	SetWheelSpeeds(0.f, 0.f, 0.f, 0.f);
	NextConnectTime = FPlatformTime::Seconds() + 1.0;
}

void AMareMuJoCoRoverBridge::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Rover)
		return;
	Bind();

	StartSimulation();
	StepPhysics(DeltaSeconds);
	AttachSensorRig();
	BindRigWheels();
	SpinRigWheels();

	if (!Socket)
	{
		if (FPlatformTime::Seconds() >= NextConnectTime)
			Connect();
	}
	else
	{
		const ESocketConnectionState State = Socket->GetConnectionState();
		if (State == SCS_Connected)
		{
			if (!bConnected)
			{
				bConnected = true;
				UE_LOG(LogMareBridge, Log, TEXT("Connected to the rover node at %s:%d"), *Host, Port);
			}
			Receive();
		}
		else if (State == SCS_ConnectionError || (bConnected && State != SCS_Connected))
		{
			Disconnect(TEXT("connection lost"));
		}
		else if (!bConnected && FPlatformTime::Seconds() - ConnectStartTime > 3.0)
		{
			Disconnect(TEXT("connect timed out"));
		}
	}

	if (CommandTimeout > 0.f && LastCommandTime >= 0.0 && FPlatformTime::Seconds() - LastCommandTime > CommandTimeout)
	{
		SetWheelSpeeds(0.f, 0.f, 0.f, 0.f);
		LastCommandTime = -1.0;
	}
	SendTelemetry(DeltaSeconds);
}

void AMareMuJoCoRoverBridge::Receive()
{
	uint32 Pending = 0;
	uint8 Buffer[4096];
	while (Socket && Socket->HasPendingData(Pending))
	{
		int32 Read = 0;
		if (!Socket->Recv(Buffer, sizeof(Buffer), Read) || Read <= 0)
		{
			Disconnect(TEXT("peer closed"));
			return;
		}
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Buffer), Read);
		Inbox.AppendChars(Text.Get(), Text.Length());
	}
	// Extract complete JSON objects; the stream has no reliable delimiter.
	int32 Depth = 0, Start = INDEX_NONE, Consumed = 0;
	for (int32 i = 0; i < Inbox.Len(); ++i)
	{
		const TCHAR C = Inbox[i];
		if (C == TEXT('{'))
		{
			if (Depth++ == 0)
				Start = i;
		}
		else if (C == TEXT('}') && Depth > 0 && --Depth == 0)
		{
			HandleMessage(Inbox.Mid(Start, i - Start + 1));
			Consumed = i + 1;
		}
	}
	Inbox.RightChopInline(Consumed);
	if (Inbox.Len() > 65536)
		Inbox.Reset();
}

void AMareMuJoCoRoverBridge::HandleMessage(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Obj) || !Obj.IsValid())
		return;
	double In[4];
	const TCHAR* Keys[4] = {TEXT("FL"), TEXT("BL"), TEXT("FR"), TEXT("BR")};
	for (int32 k = 0; k < 4; ++k)
		if (!Obj->TryGetNumberField(Keys[k], In[k]))
			return;
	// Wheel input -> surface speed (m/s) -> angular velocity (rad/s), positive = forward.
	const double Scale = (bNegativeIsForward ? -1.0 : 1.0) / FMath::Max(1e-6, double(WheelInputPerMps) * WheelRadius);
	SetWheelSpeeds(In[0] * Scale, In[1] * Scale, In[2] * Scale, In[3] * Scale);
	LastCommandTime = FPlatformTime::Seconds();

	// Steering angles (rad, positive = left); a missing one holds the wheel straight.
	double Steer[4] = {0.0, 0.0, 0.0, 0.0};
	const TCHAR* SteerKeys[4] = {TEXT("SFL"), TEXT("SBL"), TEXT("SFR"), TEXT("SBR")};
	for (int32 k = 0; k < 4; ++k)
		Obj->TryGetNumberField(SteerKeys[k], Steer[k]);
	SetSteeringAngles(Steer[0], Steer[1], Steer[2], Steer[3]);

	// Headlight state: headlights_on, headlights_intensity (0..1).
	bool bOn = false;
	double Level = 1.0;
	if (Obj->TryGetBoolField(TEXT("headlights_on"), bOn))
	{
		Obj->TryGetNumberField(TEXT("headlights_intensity"), Level);
		SetHeadlights(bOn, Level);
	}
}

void AMareMuJoCoRoverBridge::SendTelemetry(float DeltaSeconds)
{
	if (!BaseBody)
		return;
	// When this actor steps, read MuJoCo's state directly: components are updated later in the frame.
	const bool bOwnStep = bStepWithGameTime && Manager.IsValid() && !Manager->IsRunning();
	const FVector Loc = bOwnStep ? BaseBody->GetWorldPosition() : BaseBody->GetComponentLocation();
	const FQuat Rot = bOwnStep ? BaseBody->GetWorldRotation() : BaseBody->GetComponentQuat();

	TelemetryAccum += DeltaSeconds;
	const double Period = 1.0 / FMath::Max(1.0f, TelemetryHz);
	if (TelemetryAccum < Period)
		return;
	const double Dt = TelemetryAccum;
	TelemetryAccum = 0.0;

	// World-frame velocities from consecutive poses (cm/s, rad/s), matching the Chaos Blueprint's telemetry.
	FVector V = FVector::ZeroVector, W = FVector::ZeroVector;
	if (bHavePrevPose && Dt > 0.0)
	{
		V = (Loc - PrevLocation) / Dt;
		FQuat Dq = Rot * PrevRotation.Inverse();
		Dq.EnforceShortestArcWith(FQuat::Identity);
		FVector Axis;
		float Angle;
		Dq.ToAxisAndAngle(Axis, Angle);
		W = Axis * (Angle / Dt);
	}
	PrevLocation = Loc;
	PrevRotation = Rot;
	bHavePrevPose = true;

	if (!bConnected || !Socket)
		return;
	const FRotator R = Rot.Rotator();
	const FString Msg = FString::Printf(
		TEXT("{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"roll\":%.4f,\"pitch\":%.4f,\"yaw\":%.4f,")
		TEXT("\"vx\":%.3f,\"vy\":%.3f,\"vz\":%.3f,\"wx\":%.5f,\"wy\":%.5f,\"wz\":%.5f,\"t_ns\":\"%s\"}\n"),
		Loc.X, Loc.Y, Loc.Z, R.Roll, R.Pitch, R.Yaw, V.X, V.Y, V.Z, W.X, W.Y, W.Z,
		*UGncSimTimeLibrary::GetSimTimeNsString(this));
	const FTCHARToUTF8 Utf8(*Msg);
	int32 Sent = 0;
	if (!Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent))
		Disconnect(TEXT("send failed"));
}
