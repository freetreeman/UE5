// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in //UE5/Private-Frosty stream
struct CORE_API FUE5PrivateFrostyStreamObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Added HLODBatchingPolicy member to UPrimitiveComponent, which replaces the confusing bUseMaxLODAsImposter & bBatchImpostersAsInstances.
		HLODBatchingPolicy,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	FUE5PrivateFrostyStreamObjectVersion() = delete;
};
