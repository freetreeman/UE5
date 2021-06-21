// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdMode.h"
#include "ContextualAnimEdModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "ContextualAnimEdModeSettings.h"
#include "DrawDebugHelpers.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimManager.h"
#include "ContextualAnimPreviewManager.h"
#include "ContextualAnimSceneActorComponent.h"
#include "GameFramework/Character.h"
#include "ContextualAnimMetadata.h"
#include "ContextualAnimSceneInstance.h"

const FEditorModeID FContextualAnimEdMode::EM_ContextualAnimEdModeId = TEXT("EM_ContextualAnimEdMode");

FContextualAnimEdMode::FContextualAnimEdMode()
{
	PreviewManager = NewObject<UContextualAnimPreviewManager>(UContextualAnimPreviewManager::StaticClass());
}

FContextualAnimEdMode::~FContextualAnimEdMode()
{
}

void FContextualAnimEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdMode::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(PreviewManager);
}

void FContextualAnimEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FContextualAnimEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FContextualAnimEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

TSharedPtr<FContextualAnimEdModeToolkit> FContextualAnimEdMode::GetContextualAnimEdModeToolkit() const
{
	return StaticCastSharedPtr<FContextualAnimEdModeToolkit>(Toolkit);
}

void FContextualAnimEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if(GEditor->IsSimulatingInEditor())
	{
		if (!ViewportClient->Viewport->KeyState(EKeys::RightMouseButton))
		{
			if (ViewportClient->Viewport->KeyState(EKeys::W) || ViewportClient->Viewport->KeyState(EKeys::S))
			{
				PreviewManager->MoveForward(ViewportClient->Viewport->KeyState(EKeys::W) ? 1.f : -1.f);
			}

			if (ViewportClient->Viewport->KeyState(EKeys::A) || ViewportClient->Viewport->KeyState(EKeys::D))
			{
				PreviewManager->MoveRight(ViewportClient->Viewport->KeyState(EKeys::D) ? 1.f : -1.f);
			}
		}
	}
	else
	{
		if(PreviewManager)
		{
			if (PreviewManager->bDrawDebugScene)
			{
				if (UContextualAnimSceneAsset* Asset = GetContextualAnimEdModeToolkit()->GetSettings()->SceneAsset)
				{
					DrawDebugCoordinateSystem(GetWorld(), PreviewManager->ScenePivot.GetLocation(), PreviewManager->ScenePivot.Rotator(), 50.f, false, 0.f, 0, 1.f);
					UContextualAnimUtilities::DrawDebugScene(GetWorld(), Asset, PreviewManager->AnimDataIndex, PreviewManager->Time, PreviewManager->ScenePivot, FColor::White, 0.f, 1.f);
				}
			}

			//@TODO: Move this to an event that triggers after stop Simulating Mode (not sure if we have one)
			if(PreviewManager->PreviewActors.Num())
			{
				PreviewManager->PreviewActors.Reset();
			}
		}
	}
}

bool FContextualAnimEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if(Key == EKeys::Enter && Event == IE_Released && PreviewManager->TestCharacter.IsValid() && GEditor->IsSimulatingInEditor())
	{
		UContextualAnimManager* Manager = UContextualAnimManager::Get(GetWorld());
		check(Manager);		

		if(!Manager->IsActorInAnyScene(PreviewManager->TestCharacter.Get()))
		{
			UContextualAnimSceneAsset* Asset = GetContextualAnimEdModeToolkit()->GetSettings()->SceneAsset;
			if (Asset && PreviewManager->PreviewActors.Num() > 0)
			{
				FContextualAnimSceneBindings Bindings;
				Bindings.RoleToActorMap = PreviewManager->PreviewActors;
				Manager->TryStartScene(Asset, Bindings);
			}
			else
			{
				if (UContextualAnimSceneActorComponent* Comp = Manager->FindClosestSceneActorCompToActor(PreviewManager->TestCharacter.Get()))
				{
					TSet<UContextualAnimSceneActorComponent*> Comps = Manager->GetSceneActorCompContainer();

					Comps.Sort([Comp](const UContextualAnimSceneActorComponent& A, const UContextualAnimSceneActorComponent& B){
						const float DistSqA = FVector::DistSquared2D(Comp->GetComponentLocation(), A.GetComponentLocation());
						const float DistSqB = FVector::DistSquared2D(Comp->GetComponentLocation(), B.GetComponentLocation());
						return DistSqA < DistSqB;
					});

					Manager->TryStartScene(Comp->SceneAsset, Comp->GetOwner(), Comps);
				}
				else
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING: Can't find any SceneActor to interact with")));
					UE_LOG(LogContextualAnim, Warning, TEXT("WARNING: Can't find any SceneActor to interact with"));
				}
			}
		}
		else
		{
			Manager->TryStopSceneWithActor(PreviewManager->TestCharacter.Get());
		}

		return true;
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FContextualAnimEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!Click.IsAltDown())
	{
		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}

	if (!GEditor->IsSimulatingInEditor())
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING. You are not in Simulating Mode")));
		UE_LOG(LogContextualAnim, Warning, TEXT("WARNING. You are not in Simulating Mode"));

		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}

	FHitResult HitResult;
	GetHitResultUnderCursor(HitResult, InViewportClient, Click);

	const FString Message = FString::Printf(TEXT("INFO: HandleClick: IsValidHit: %d Actor: %s Location: %s"),
		HitResult.IsValidBlockingHit(), *GetNameSafe(HitResult.GetActor()), *HitResult.ImpactPoint.ToString());

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::White, Message);
	UE_LOG(LogContextualAnim, Warning, TEXT("%s"), *Message);

	if (!HitResult.IsValidBlockingHit())
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING. HitResult from click event is not a valid blocking hit.")));
		UE_LOG(LogContextualAnim, Warning, TEXT("WARNING. HitResult from click event is not a valid blocking hit."));

		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}	
	
	FTransform SpawnTransform = FTransform(HitResult.ImpactPoint);
	UContextualAnimSceneAsset* Asset = GetContextualAnimEdModeToolkit()->GetSettings()->SceneAsset;
	if (Asset && PreviewManager->PreviewActors.Num() == 0)
	{
		PreviewManager->SpawnPreviewActors(Asset, SpawnTransform); 
	}
	else
	{
		if (!PreviewManager->TestCharacter.IsValid())
		{
			PreviewManager->TestCharacter = Cast<ACharacter>(PreviewManager->SpawnPreviewActor(PreviewManager->DefaultPreviewClass, SpawnTransform));
		}
		else
		{
			PreviewManager->MoveToLocation(HitResult.ImpactPoint);
		}
	}

	return true;
}

bool FContextualAnimEdMode::GetHitResultUnderCursor(FHitResult& OutHitResult, FEditorViewportClient* InViewportClient, const FViewportClick& Click) const
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags).SetRealtimeUpdate(InViewportClient->IsRealtime()));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation Cursor(View, InViewportClient, Click.GetClickPos().X, Click.GetClickPos().Y);
	const auto ViewportType = InViewportClient->GetViewportType();

	const FVector RayStart = Cursor.GetOrigin();
	const FVector RayEnd = RayStart + Cursor.GetDirection() * HALF_WORLD_MAX;

	return InViewportClient->GetWorld()->LineTraceSingleByChannel(OutHitResult, RayStart, RayEnd, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam);
}

bool FContextualAnimEdMode::UsesToolkits() const
{
	return true;
}

FContextualAnimEdMode& FContextualAnimEdMode::Get()
{
	return *(static_cast<FContextualAnimEdMode*>(GLevelEditorModeTools().GetActiveMode(FContextualAnimEdMode::EM_ContextualAnimEdModeId)));
}

