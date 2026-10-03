// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoCoreSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoCoreTimeSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FTempoCoreRenderingSettingsChanged);

/**
 * TempoCore Plugin Settings.
 */
UCLASS(Config=Plugins, DefaultConfig)
class TEMPOCORE_API UTempoCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTempoCoreSettings();

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	// Allow command-line overrides
	virtual void PostInitProperties() override;

	// Time Settings.
	void SetTimeMode(ETimeMode TimeModeIn);
	void SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn);
	ETimeMode GetTimeMode() const { return TimeMode; }
	double GetMaxWallClockTimeStep() const { return MaxWallClockTimeStep; }
	int32 GetSimulatedStepsPerSecond() const { return SimulatedStepsPerSecond; }
	FTempoCoreTimeSettingsChanged TempoCoreTimeSettingsChangedEvent;
	FTempoCoreRenderingSettingsChanged TempoCoreRenderingSettingsChanged;

	// Server Settings.
	EServerTransport GetServerTransport() const { return ServerTransport; }
	int32 GetServerPort() const { return ServerPort; }
	const FString& GetServerSocketPath() const { return ServerSocketPath; }
	EServerCompressionLevel GetServerCompressionLevel() const { return ServerCompressionLevel; }
	bool GetFatalOnServerStartFailure() const { return bFatalOnServerStartFailure; }
	int32 GetMaxEventProcessingTime() const { return MaxEventProcessingTimeMicroSeconds; }
	int32 GetMaxEventWaitTime() const { return MaxEventWaitTimeNanoSeconds; }

	// Packaging Settings.
	bool GetAssignLevelsToIndividualChunks() const { return bAssignLevelsToIndividualChunks; }

	// Rendering Settings.
	bool GetRenderMainViewport() const { return bRenderMainViewport; }
	void SetRenderMainViewport(bool bInRenderMainViewport);

	// Control Settings.
	EControlMode GetDefaultControlMode() const { return DefaultControlMode; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	static FName GetServerTransportMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ServerTransport); }
	static FName GetServerPortMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ServerPort); }
	static FName GetServerSocketPathMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ServerSocketPath); }
	static FName GetServerCompressionLevelMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ServerCompressionLevel); }
#endif

private:
	UPROPERTY(EditAnywhere, Config, Category="Time")
	ETimeMode TimeMode = ETimeMode::WallClock;

	// The number of evenly-spaced steps per simulated second that will be executed in FixedStep time mode.
	UPROPERTY(EditAnywhere, Config, Category="Time|FixedStep", meta=(ClampMin=1, UIMin=1, UIMax=100))
	int32 SimulatedStepsPerSecond = 10;

	// The largest time step allowed in WallClock time mode. No limit if 0.0. Note that setting this to non-zero violates
	// WallClock time mode's guarantee of strictly advancing along with wall clock at the step that would have exceeded the max.
	UPROPERTY(EditAnywhere, Config, Category="Time|WallClock", meta=(ClampMin=0.0, UIMin=0.0, UIMax=1.0))
	double MaxWallClockTimeStep = 0.0;

	// Whether the Tempo gRPC server listens on a TCP port or a Unix domain socket. A socket keeps
	// several servers on one machine from competing for ports, but is reachable only from that
	// machine. Only one transport is used at a time.
	UPROPERTY(EditAnywhere, Config, Category="Server")
	EServerTransport ServerTransport = EServerTransport::Tcp;

	// The port number the Tempo gRPC server listens on, with the Tcp transport.
	UPROPERTY(EditAnywhere, Config, Category="Server", meta=(ClampMin=1024, ClampMax=65535, UIMin=1024, UIMax=65535,
		EditCondition="ServerTransport == EServerTransport::Tcp"))
	int32 ServerPort = 10001;

	// The Unix domain socket the Tempo gRPC server listens on, with the UnixSocket transport.
	// A bare name ("sim-a.sock") goes in a short, user-private directory chosen per platform
	// ($XDG_RUNTIME_DIR/tempo on Linux, /tmp/tempo-<uid> on macOS); anything else is used as a
	// path. Empty means "tempo.sock" in that same directory. Clients must name the same path.
	UPROPERTY(EditAnywhere, Config, Category="Server", meta=(
		EditCondition="ServerTransport == EServerTransport::UnixSocket"))
	FString ServerSocketPath;

	// Whether a packaged, headless sim that cannot claim its endpoint - because the port or socket
	// is taken, or the configuration is invalid - should exit with a fatal error instead of running
	// on without a server. Such a sim has no window to warn in and usually nobody watching, so the
	// failure would otherwise surface only as every client failing to connect. An editor session
	// and a windowed game only ever log the error, whatever this is set to. Can also be turned off
	// for a single run with -AllowServerStartFailure.
	UPROPERTY(EditAnywhere, Config, Category="Server")
	bool bFatalOnServerStartFailure = true;

	// The default compression level to use for Tempo server messages. When the client is on the same machine no
	// compression is fastest. Otherwise, compression may help reduce network bandwidth.
	UPROPERTY(EditAnywhere, Config, Category="Server")
	EServerCompressionLevel ServerCompressionLevel = EServerCompressionLevel::None;

	// We will spend as much as this amount of time (in microseconds) processing events each Tick.
	// Except in FixedStep mode, where we process all received events every Tick.
	UPROPERTY(EditAnywhere, Config, Category="Server|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventProcessingTimeMicroSeconds = 1000;

	// We will wait as much as this amount of time (in nanoseconds) for an event to arrive each time we check for an event.
	UPROPERTY(EditAnywhere, Config, Category="Server|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventWaitTimeNanoSeconds = 1000;

	// If true, each level will be assigned to its own chunk during packaging.
	// **NOTE** Requires enabling project packaging settings UsePakFile and GenerateChunks.
	UPROPERTY(EditAnywhere, Config, Category="Packaging")
	bool bAssignLevelsToIndividualChunks = false;

	// Whether to render the main viewport (or not, saving performance).
	UPROPERTY(EditAnywhere, Config, Category="Rendering")
	bool bRenderMainViewport;

	// The default control mode to use.
	UPROPERTY(EditAnywhere, Config, Category="Control")
	EControlMode DefaultControlMode = EControlMode::None;
};
