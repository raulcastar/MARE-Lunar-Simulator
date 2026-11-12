#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MareMuJoCoRoverBridge.generated.h"

class AMjArticulation;
class AAMjManager;
class UMjActuator;
class UMjBody;
class UStaticMeshComponent;
class USpotLightComponent;
class FSocket;

/**
 * Connects a URLab rover articulation to the GNC rover node using the Chaos rover Blueprint's protocol.
 *
 * - Transport: TCP client to the rover node (default 127.0.0.1:3003).
 * - Input: JSON objects with wheel inputs FL, BL, FR, BR (m/s x wheel_speed_scale, negative = forward)
 *   and the headlight state; each wheel input sets a MuJoCo velocity actuator (rad/s).
 * - Output: base-body telemetry in Unreal world units: x, y, z (cm), roll, pitch, yaw (deg),
 *   vx, vy, vz (cm/s), wx, wy, wz (rad/s) and t_ns (UnrealSimClock).
 *
 * Also attaches an optional sensor rig to the base body and steps MuJoCo with the game clock.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MARE MuJoCo Rover Bridge"))
class MAREMUJOCOBRIDGE_API AMareMuJoCoRoverBridge : public AActor
{
	GENERATED_BODY()

public:
	AMareMuJoCoRoverBridge();

	/** Articulation to drive. If unset, the first AMjArticulation in the level. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Rover")
	TObjectPtr<AMjArticulation> Rover;

	/** Body reported as base_link (axle height, centred between the wheels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	FString BaseBodyName = TEXT("base_link");

	/**
	 * Actor carrying the sensors and lights (a copy of the Chaos rover Blueprint). Its physics, collision and
	 * TCP link are disabled and it is attached so that SensorRigAnchor coincides with base_link.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Rover")
	TObjectPtr<AActor> SensorRig;

	/** Component of SensorRig that corresponds to base_link. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	FName SensorRigAnchor = TEXT("base_link");

	/** Render the rig's meshes, with wheels driven by the MuJoCo joints, instead of the MJCF visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	bool bUseRigVisuals = true;

	/** Wire key (FL, BL, FR, BR) to MuJoCo actuator name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	TMap<FString, FString> WheelActuators;

	/** Wheel input units per m/s; must match the rover node's wheel_speed_scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	float WheelInputPerMps = 1.82f;

	/** Wheel radius (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	float WheelRadius = 0.0885f;

	/** Wire sign convention: negative wheel input drives forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	bool bNegativeIsForward = true;

	/** Stop the wheels if no command arrives within this time (s); 0 disables the timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover")
	float CommandTimeout = 1.0f;

	/** Unpause the MuJoCo simulation when Play starts (URLab starts paused). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bStartSimulationOnPlay = true;

	/**
	 * Advance MuJoCo by each frame's game time from this actor instead of URLab's real-time loop, so physics,
	 * cameras and telemetry share Unreal's clock. URLab's pause toggle (P) returns stepping to URLab.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bStepWithGameTime = true;

	/** Upper bound on MuJoCo steps per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerFrame = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	FString Host = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	int32 Port = 3003;

	/** Telemetry rate (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	float TelemetryHz = 30.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Connection")
	bool bConnected = false;

	/** Sets the wheel angular velocities directly (rad/s, positive = forward). */
	UFUNCTION(BlueprintCallable, Category = "Rover")
	void SetWheelSpeeds(float FL, float BL, float FR, float BR);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FRigWheel
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FString Joint;            // MuJoCo hinge driving the mesh
		FQuat Rest;               // relative rotation at rest
		FVector AxleInParent;     // wheel axle in the mesh's parent frame
	};

	bool Bind();
	void StartSimulation();
	void StepPhysics(float DeltaSeconds);
	void PrepareSensorRig();
	void AttachSensorRig();
	void BindRigWheels();
	void SpinRigWheels();
	void SetHeadlights(bool bOn, double Level);
	void Connect();
	void Disconnect(const TCHAR* Why);
	void Receive();
	void HandleMessage(const FString& Json);
	void SendTelemetry(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UMjBody> BaseBody;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMjActuator>> Actuators;

	TWeakObjectPtr<AAMjManager> Manager;
	bool bSimulationStarted = false;
	double StepAccum = 0.0;

	bool bRigAttached = false;
	bool bRigWheelsBound = false;
	TArray<FRigWheel> RigWheels;
	TMap<TWeakObjectPtr<USpotLightComponent>, float> HeadlightMax;

	FSocket* Socket = nullptr;
	FString Inbox;
	double NextConnectTime = 0.0;
	double ConnectStartTime = 0.0;
	double LastCommandTime = -1.0;
	bool bWarnedBind = false;

	double TelemetryAccum = 0.0;
	bool bHavePrevPose = false;
	FVector PrevLocation = FVector::ZeroVector;
	FQuat PrevRotation = FQuat::Identity;
};
