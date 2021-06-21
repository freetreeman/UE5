// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"

#include "CoreMinimal.h"

class IAssetTypeActions;

/**
 * Implements the CameraCalibrationEditor module.
 */
class FCameraCalibrationEditorModule : public IModuleInterface
{
public:

	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

private:

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};
