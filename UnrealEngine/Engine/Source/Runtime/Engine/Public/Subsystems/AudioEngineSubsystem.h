// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystem.h"
#include "SubsystemCollection.h"
#include "AudioDeviceManager.h"
#include "AudioEngineSubsystem.generated.h"

// Forward Declarations 
class FAudioDevice;

/**
 * UAudioSubsystemCollectionRoot
 * Root UObject used to anchor UAudioEngineSubsystems to the FAudioDevice
 */
UCLASS()
class ENGINE_API UAudioSubsystemCollectionRoot final : public UObject
{
	GENERATED_BODY()

public:

	/** Set the ID of the owning audio device */
	void FORCEINLINE SetAudioDeviceID(Audio::FDeviceId DeviceID) { OwningDeviceID = DeviceID; }

	/** Get the ID of the owning audio device */
	Audio::FDeviceId GetAudioDeviceID() const { return OwningDeviceID; }

protected:

	Audio::FDeviceId OwningDeviceID = INDEX_NONE;
};

/**
 * UAudioEngineSubsystem
 * Base class for auto instanced and initialized systems that share the lifetime of the audio device
 */
UCLASS(Abstract)
class ENGINE_API UAudioEngineSubsystem : public UDynamicSubsystem
{
	GENERATED_BODY()

public:

	UAudioEngineSubsystem();

	virtual FAudioDeviceHandle GetAudioDeviceHandle() const final;
};

/**
 * FAudioSubsystemCollection - Subsystem collection specifically targeting UAudioEngineSubsystems
 */
class FAudioSubsystemCollection : public FSubsystemCollection<UAudioEngineSubsystem>
{
public:

	template<class InterfaceToCastTo>
	void ForEachSubsystem(TFunctionRef<bool(InterfaceToCastTo*)> InFunction) const
	{
		const TArray<USubsystem*>& AllSubsystems = GetSubsystemArrayInternal(UAudioEngineSubsystem::StaticClass());
		for (USubsystem* Subsystem : AllSubsystems)
		{
			if (InterfaceToCastTo* CastedSystem = Cast<InterfaceToCastTo>(Subsystem))
			{
				InFunction(CastedSystem);
			}
		}
	}
};

