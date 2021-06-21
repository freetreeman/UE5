// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialGraphNode_Base.cpp
=============================================================================*/

#include "MaterialGraph/MaterialGraphNode_Base.h"
#include "EdGraph/EdGraphSchema.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"

/////////////////////////////////////////////////////
// UMaterialGraphNode_Base

UMaterialGraphNode_Base::UMaterialGraphNode_Base(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const FMaterialGraphPinInfo& UMaterialGraphNode_Base::GetPinInfo(const class UEdGraphPin* Pin) const
{
	const FMaterialGraphPinInfo* PinInfo = PinInfoMap.Find(Pin);
	checkf(PinInfo, TEXT("Missing info for pin %s, missing call to RegisterPin()?"), *Pin->GetName());
	return *PinInfo;
}

uint32 UMaterialGraphNode_Base::GetOutputType(const UEdGraphPin* OutputPin) const
{
	return GetPinMaterialType(OutputPin, GetPinInfo(OutputPin));
}

uint32 UMaterialGraphNode_Base::GetInputType(const UEdGraphPin* InputPin) const
{
	return GetPinMaterialType(InputPin, GetPinInfo(InputPin));
}

uint32 UMaterialGraphNode_Base::GetPinMaterialType(const UEdGraphPin* Pin, const FMaterialGraphPinInfo& PinInfo) const
{
	switch (PinInfo.PinType)
	{
	case EMaterialGraphPinType::Data:
		return MCT_Unknown;
	case EMaterialGraphPinType::Exec:
		return MCT_Execution;
	default:
		checkNoEntry();
		return 0u;
	}
}

void UMaterialGraphNode_Base::ReplaceNode(UMaterialGraphNode_Base* OldNode)
{
	check(OldNode);
	check(OldNode != this);

	// Copy Inputs from old node
	for (int32 PinIndex = 0; PinIndex < OldNode->InputPins.Num(); PinIndex++)
	{
		if (PinIndex < InputPins.Num())
		{
			ModifyAndCopyPersistentPinData(*InputPins[PinIndex], *OldNode->InputPins[PinIndex]);
		}
	}

	// Copy Outputs from old node
	for (int32 PinIndex = 0; PinIndex < OldNode->OutputPins.Num(); PinIndex++)
	{
		// Try to find an equivalent output in this node
		int32 FoundPinIndex = -1;
		{
			// First check names
			for (int32 NewPinIndex = 0; NewPinIndex < OutputPins.Num(); NewPinIndex++)
			{
				if (OldNode->OutputPins[PinIndex]->PinName == OutputPins[NewPinIndex]->PinName)
				{
					FoundPinIndex = NewPinIndex;
					break;
				}
			}
		}
		if (FoundPinIndex == -1)
		{
			// Now check types
			for (int32 NewPinIndex = 0; NewPinIndex < OutputPins.Num(); NewPinIndex++)
			{
				if (OldNode->OutputPins[PinIndex]->PinType == OutputPins[NewPinIndex]->PinType)
				{
					FoundPinIndex = NewPinIndex;
					break;
				}
			}
		}

		// If we can't find an equivalent output in this node, just use the first
		// The user will have to fix up any issues from the mismatch
		FoundPinIndex = FMath::Max(FoundPinIndex, 0);
		if (FoundPinIndex < OutputPins.Num())
		{
			ModifyAndCopyPersistentPinData(*OutputPins[FoundPinIndex], *OldNode->OutputPins[PinIndex]);
		}
	}

	// Break the original pin links
	for (int32 OldPinIndex = 0; OldPinIndex < OldNode->Pins.Num(); ++OldPinIndex)
	{
		UEdGraphPin* OldPin = OldNode->Pins[OldPinIndex];
		OldPin->Modify();
		OldPin->BreakAllPinLinks();
	}
}

void UMaterialGraphNode_Base::InsertNewNode(UEdGraphPin* FromPin, UEdGraphPin* NewLinkPin, TSet<UEdGraphNode*>& OutNodeList)
{
	const UMaterialGraphSchema* Schema = CastChecked<UMaterialGraphSchema>(GetSchema());

	// The pin we are creating from already has a connection that needs to be broken. We want to "insert" the new node in between, so that the output of the new node is hooked up too
	UEdGraphPin* OldLinkedPin = FromPin->LinkedTo[0];
	check(OldLinkedPin);

	FromPin->BreakAllPinLinks();

	// Hook up the old linked pin to the first valid output pin on the new node
	for (int32 OutpinPinIdx=0; OutpinPinIdx<Pins.Num(); OutpinPinIdx++)
	{
		UEdGraphPin* OutputPin = Pins[OutpinPinIdx];
		check(OutputPin);
		if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Schema->CanCreateConnection(OldLinkedPin, OutputPin).Response)
		{
			if (Schema->TryCreateConnection(OldLinkedPin, OutputPin))
			{
				OutNodeList.Add(OldLinkedPin->GetOwningNode());
				OutNodeList.Add(this);
			}
			break;
		}
	}

	if (Schema->TryCreateConnection(FromPin, NewLinkPin))
	{
		OutNodeList.Add(FromPin->GetOwningNode());
		OutNodeList.Add(this);
	}
}

void UMaterialGraphNode_Base::AllocateDefaultPins()
{
	check(Pins.Num() == 0);
	check(InputPins.Num() == 0);
	check(OutputPins.Num() == 0);
	check(PinInfoMap.Num() == 0);

	CreateInputPins();
	CreateOutputPins();
}

void UMaterialGraphNode_Base::PostPasteNode()
{
	check(PinInfoMap.Num() == 0)

	int32 NumInputDataPins = 0;
	int32 NumOutputDataPins = 0;
	int32 NumInputExecPins = 0;
	int32 NumOutputExecPins = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		int32 Index = INDEX_NONE;
		EMaterialGraphPinType Type;

		if (Pin->PinType.PinCategory == UMaterialGraphSchema::PC_Exec)
		{
			Type = EMaterialGraphPinType::Exec;
			if (Pin->Direction == EGPD_Input) Index = NumInputExecPins++;
			else Index = NumOutputExecPins++;
		}
		else
		{
			Type = EMaterialGraphPinType::Data;
			if (Pin->Direction == EGPD_Input) Index = NumInputDataPins++;
			else Index = NumOutputDataPins++;
		}
		RegisterPin(Pin, Type, Index);
	}
}

void UMaterialGraphNode_Base::RegisterPin(UEdGraphPin* Pin, EMaterialGraphPinType Type, int32 Index)
{
	FMaterialGraphPinInfo& PinInfo = PinInfoMap.FindOrAdd(Pin);
	PinInfo.PinType = Type;
	PinInfo.Index = Index;

	if (Type == EMaterialGraphPinType::Exec)
	{
		switch (Pin->Direction)
		{
		case EGPD_Input:
			checkf(ExecInputPin == nullptr, TEXT("Only 1 exec input pin allowed"));
			check(Index == 0);
			ExecInputPin = Pin;
			break;
		case EGPD_Output: verify(ExecOutputPins.Add(Pin) == Index); break;
		default: checkNoEntry(); break;
		}
	}
	else
	{
		switch (Pin->Direction)
		{
		case EGPD_Input: verify(InputPins.Add(Pin) == Index); break;
		case EGPD_Output: verify(OutputPins.Add(Pin) == Index); break;
		default: checkNoEntry(); break;
		}
	}
}

void UMaterialGraphNode_Base::EmptyPins()
{
	Pins.Reset();
	PinInfoMap.Reset();
	InputPins.Reset();
	OutputPins.Reset();
	ExecOutputPins.Reset();
	ExecInputPin = nullptr;
}

static void TransferPinArray(const TArray<UEdGraphPin*>& NewPins, const TArray<UEdGraphPin*>& OldPins)
{
	const int32 Num = FMath::Min(NewPins.Num(), OldPins.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		UEdGraphPin* OldPin = OldPins[i];
		UEdGraphPin* NewPin = NewPins[i];
		ensure(OldPin->Direction == NewPin->Direction);
		ensure(OldPin->PinType.PinCategory == NewPin->PinType.PinCategory);
		NewPin->MovePersistentDataFromOldPin(*OldPin);
	}
}

void UMaterialGraphNode_Base::ReconstructNode()
{
	Modify();

	// Break any links to 'orphan' pins
	for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
	{
		UEdGraphPin* Pin = Pins[PinIndex];
		TArray<class UEdGraphPin*>& LinkedToRef = Pin->LinkedTo;
		for (int32 LinkIdx=0; LinkIdx < LinkedToRef.Num(); LinkIdx++)
		{
			UEdGraphPin* OtherPin = LinkedToRef[LinkIdx];
			// If we are linked to a pin that its owner doesn't know about, break that link
			if (!OtherPin->GetOwningNode()->Pins.Contains(OtherPin))
			{
				Pin->LinkedTo.Remove(OtherPin);
			}
		}
	}

	// Move the existing pins to a saved array
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	TArray<UEdGraphPin*> OldInputPins = MoveTemp(InputPins);
	TArray<UEdGraphPin*> OldOutputPins = MoveTemp(OutputPins);
	TArray<UEdGraphPin*> OldExecOutputPins = MoveTemp(ExecOutputPins);
	UEdGraphPin* OldExecInputPin = ExecInputPin;

	EmptyPins();

	// Recreate the new pins
	AllocateDefaultPins();

	// Transfer data to new pins
	TransferPinArray(InputPins, OldInputPins);
	TransferPinArray(OutputPins, OldOutputPins);
	TransferPinArray(ExecOutputPins, OldExecOutputPins);
	if (OldExecInputPin && ExecInputPin)
	{
		ExecInputPin->MovePersistentDataFromOldPin(*OldExecInputPin);
	}

	for (UEdGraphPin* OldPin : OldPins)
	{
		// Throw away the original pins
		OldPin->Modify();
		UEdGraphNode::DestroyPin(OldPin);
	}

	GetGraph()->NotifyGraphChanged();
}

void UMaterialGraphNode_Base::RemovePinAt(const int32 PinIndex, const EEdGraphPinDirection PinDirection)
{
	UEdGraphPin* Pin = GetPinWithDirectionAt(PinIndex, PinDirection);
	check(Pin);

	FMaterialGraphPinInfo PinInfo;
	verify(PinInfoMap.RemoveAndCopyValue(Pin, PinInfo));
	switch (PinInfo.PinType)
	{
	case EMaterialGraphPinType::Data:
		switch (Pin->Direction)
		{
		case EGPD_Input: InputPins.RemoveAt(PinInfo.Index); break;
		case EGPD_Output: OutputPins.RemoveAt(PinInfo.Index); break;
		default: checkNoEntry(); break;
		}
		break;
	case EMaterialGraphPinType::Exec:
		switch (Pin->Direction)
		{
		case EGPD_Input: check(ExecInputPin == Pin);  ExecInputPin = nullptr; break;
		case EGPD_Output: ExecOutputPins.RemoveAt(PinInfo.Index); break;
		default: checkNoEntry(); break;
		}
		break;
	default:
		checkNoEntry();
		break;
	}

	// Shift down indices to account for the pin we removed
	for (auto& It : PinInfoMap)
	{
		if (It.Value.PinType == PinInfo.PinType &&
			It.Value.Index > PinInfo.Index &&
			It.Key->Direction == PinDirection)
		{
			It.Value.Index--;
		}
	}

	Super::RemovePinAt(PinIndex, PinDirection);

	UMaterialGraph* MaterialGraph = CastChecked<UMaterialGraph>(GetGraph());
	MaterialGraph->LinkMaterialExpressionsFromGraph();
}

void UMaterialGraphNode_Base::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin != NULL)
	{
		const UMaterialGraphSchema* Schema = CastChecked<UMaterialGraphSchema>(GetSchema());

		TSet<UEdGraphNode*> NodeList;

		// auto-connect from dragged pin to first compatible pin on the new node
		for (int32 i=0; i<Pins.Num(); i++)
		{
			UEdGraphPin* Pin = Pins[i];
			check(Pin);
			FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, Pin);
			if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Response.Response) //-V1051
			{
				if (Schema->TryCreateConnection(FromPin, Pin))
				{
					NodeList.Add(FromPin->GetOwningNode());
					NodeList.Add(this);
				}
				break;
			}
			else if(ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_A == Response.Response)
			{
				InsertNewNode(FromPin, Pin, NodeList);
				break;
			}
		}

		// Send all nodes that received a new pin connection a notification
		for (auto It = NodeList.CreateConstIterator(); It; ++It)
		{
			UEdGraphNode* Node = (*It);
			Node->NodeConnectionListChanged();
		}
	}
}

bool UMaterialGraphNode_Base::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UMaterialGraphSchema::StaticClass());
}

void UMaterialGraphNode_Base::ModifyAndCopyPersistentPinData(UEdGraphPin& TargetPin, const UEdGraphPin& SourcePin) const
{
	if (SourcePin.LinkedTo.Num() > 0)
	{
		TargetPin.Modify();

		for (int32 LinkIndex = 0; LinkIndex < SourcePin.LinkedTo.Num(); ++LinkIndex)
		{
			UEdGraphPin* OtherPin = SourcePin.LinkedTo[LinkIndex];
			OtherPin->Modify();
		}
	}

	TargetPin.CopyPersistentDataFromOldPin(SourcePin);
}

FString UMaterialGraphNode_Base::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Material");
}

