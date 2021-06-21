// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "Algo/Sort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLiteral.h"
#include "MetasoundUObjectRegistry.h"
#include "Modules/ModuleManager.h"
#include "Templates/Tuple.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound
{
	namespace Editor
	{
		const FName FGraphBuilder::PinCategoryAudio = "audio";
		const FName FGraphBuilder::PinCategoryBoolean = "bool";
		//const FName FGraphBuilder::PinCategoryDouble = "double";
		const FName FGraphBuilder::PinCategoryFloat = "float";
		const FName FGraphBuilder::PinCategoryInt32 = "int";
		//const FName FGraphBuilder::PinCategoryInt64 = "int64";
		const FName FGraphBuilder::PinCategoryObject = "object";
		const FName FGraphBuilder::PinCategoryString = "string";
		const FName FGraphBuilder::PinCategoryTrigger = "trigger";

		const FName FGraphBuilder::PinSubCategoryTime = "time";

		const FText FGraphBuilder::ConvertMenuName = LOCTEXT("MetasoundConversionsMenu", "Conversions");
		const FText FGraphBuilder::FunctionMenuName = LOCTEXT("MetasoundFunctionsMenu", "Functions");

		namespace GraphBuilderPrivate
		{
			namespace NodeLayout
			{
				static const FVector2D BufferX { 250.0f, 0.0f };
				static const FVector2D BufferY { 0.0f, 100.0f };
			}

			void DeleteNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle)
			{
				if (InNodeHandle->IsValid())
				{
					Frontend::FGraphHandle GraphHandle = InNodeHandle->GetOwningGraph();
					if (GraphHandle->IsValid())
					{
						GraphHandle->RemoveNode(*InNodeHandle);
					}
				}
			}

			void InitializeGraph(UObject& InMetaSound)
			{
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
				check(MetaSoundAsset);

				// Initial graph generation is not something to be managed by the transaction stack, so don't track dirty state until
				// after initial setup if necessary.
				if (!MetaSoundAsset->GetGraph())
				{
					UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(&InMetaSound, FName(), RF_Transactional);
					Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
					MetaSoundAsset->SetGraph(Graph);
				}
			}
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			UMetasoundEditorGraphExternalNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassMetadata().Type == EMetasoundFrontendClassType::External))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);
			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphExternalNode> NodeCreator(Graph);

			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			NewGraphNode->ClassName = InNodeHandle->GetClassMetadata().ClassName;

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetasound);

			SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

			return NewGraphNode;
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetasound, const FMetasoundFrontendClassMetadata& InMetadata, FVector2D InLocation, bool bInSelectNewNode)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FNodeHandle NodeHandle = MetasoundAsset->GetRootGraphHandle()->AddNode(InMetadata);
			return AddExternalNode(InMetasound, NodeHandle, InLocation, bInSelectNewNode);
		}

		UMetasoundEditorGraphOutputNode* FGraphBuilder::AddOutputNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			UMetasoundEditorGraphOutputNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassMetadata().Type == EMetasoundFrontendClassType::Output))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);
			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphOutputNode> NodeCreator(Graph);

			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(&Graph);
			NewGraphNode->Output = MetasoundGraph->FindOrAddOutput(InNodeHandle);

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetasound);

			SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

			return NewGraphNode;
		}

		void FGraphBuilder::InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetasound)
		{
			NewGraphNode->CreateNewGuid();
			NewGraphNode->SetNodeID(InNodeHandle->GetID());

			RebuildNodePins(*NewGraphNode);
		}

		TArray<FString> FGraphBuilder::GetDataTypeNameCategories(const FName& InDataTypeName)
		{
			FString CategoryString = InDataTypeName.ToString();

			TArray<FString> Categories;
			CategoryString.ParseIntoArray(Categories, TEXT(":"));

			if (Categories.Num() > 0)
			{
				// Remove name
				Categories.RemoveAt(Categories.Num() - 1);
			}

			return Categories;
		}

		FText FGraphBuilder::GenerateUniqueInputDisplayName(const UObject& InMetaSound, const FText* InBaseName)
		{
			static const FText DefaultBaseName = FText::FromString(TEXT("Input"));
			const FText& NameBase = InBaseName ? *InBaseName : DefaultBaseName;
			return GenerateUniqueNameByFilter(InMetaSound, NameBase, [](const Frontend::FConstGraphHandle& GraphHandle, const FText& NewName)
			{
				bool bIsNameInvalid = false;
				GraphHandle->IterateConstNodes([&](Frontend::FConstNodeHandle Node)
				{
					if (NewName.CompareToCaseIgnored(Node->GetDisplayName()) == 0)
					{
						bIsNameInvalid = true;
					}
				}, EMetasoundFrontendClassType::Input);
				return !bIsNameInvalid;
			});
		}

		FText FGraphBuilder::GenerateUniqueOutputDisplayName(const UObject& InMetaSound, const FText* InBaseName)
		{
			static const FText DefaultBaseName = FText::FromString(TEXT("Output"));
			const FText& NameBase = InBaseName ? *InBaseName : DefaultBaseName;
			return GenerateUniqueNameByFilter(InMetaSound, NameBase, [](const Frontend::FConstGraphHandle& GraphHandle, const FText& NewName)
			{
				bool bIsNameInvalid = false;
				GraphHandle->IterateConstNodes([&](Frontend::FConstNodeHandle Node)
				{
					if (NewName.CompareToCaseIgnored(Node->GetDisplayName()) == 0)
					{
						bIsNameInvalid = true;
					}
				}, EMetasoundFrontendClassType::Output);
				return !bIsNameInvalid;
			});
		}

		FText FGraphBuilder::GenerateUniqueNameByFilter(const UObject& InMetaSound, const FText& InBaseText, TUniqueFunction<bool(const Frontend::FConstGraphHandle&, const FText&)> InIsValidNameFilter)
		{
			const FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetasoundAsset);

			const Frontend::FConstGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			static const FText VariableUniqueNameFormat = LOCTEXT("VariableUniqueNameFormat", "{0}{1}");

			int32 i = 1;
			FText NewName = FText::Format(VariableUniqueNameFormat, InBaseText, i);

			while (!InIsValidNameFilter(GraphHandle, NewName))
			{
				NewName = FText::Format(VariableUniqueNameFormat, InBaseText, ++i);
			}

			return NewName;
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForGraph(const UObject& Metasound)
		{
			TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(CastChecked<const UObject>(&Metasound));
			return StaticCastSharedPtr<FEditor, IToolkit>(FoundAssetEditor);
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForGraph(const UEdGraph& EdGraph)
		{
			const UMetasoundEditorGraph* MetasoundGraph = CastChecked<const UMetasoundEditorGraph>(&EdGraph);
			return GetEditorForGraph(MetasoundGraph->GetMetasoundChecked());
		}

		FLinearColor FGraphBuilder::GetPinCategoryColor(const FEdGraphPinType& PinType)
		{
			const UMetasoundEditorSettings* Settings = GetDefault<UMetasoundEditorSettings>();
			check(Settings);

			if (PinType.PinCategory == PinCategoryAudio)
			{
				return Settings->AudioPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryTrigger)
			{
				return Settings->TriggerPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryBoolean)
			{
				return Settings->BooleanPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryFloat)
			{
				if (PinType.PinSubCategory == PinSubCategoryTime)
				{
					return Settings->TimePinTypeColor;
				}
				return Settings->FloatPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryInt32)
			{
				return Settings->IntPinTypeColor;
			}

			//if (PinType.PinCategory == PinCategoryInt64)
			//{
			//	return Settings->Int64PinTypeColor;
			//}

			if (PinType.PinCategory == PinCategoryString)
			{
				return Settings->StringPinTypeColor;
			}

			//if (PinType.PinCategory == PinCategoryDouble)
			//{
			//	return Settings->DoublePinTypeColor;
			//}

			if (PinType.PinCategory == PinCategoryObject)
			{
				return Settings->ObjectPinTypeColor;
			}

			return Settings->DefaultPinTypeColor;
		}

		Frontend::FInputHandle FGraphBuilder::GetInputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin && ensure(InPin->Direction == EGPD_Input))
			{
				if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					FNodeHandle NodeHandle = EdNode->GetNodeHandle();
					if (NodeHandle->IsValid())
					{
						TArray<FInputHandle> Inputs = NodeHandle->GetInputsWithVertexName(InPin->GetName());
						if (ensure(Inputs.Num() == 1))
						{
							return Inputs[0];
						}
					}
				}
			}

			return IInputController::GetInvalidHandle();
		}

		Frontend::FConstInputHandle FGraphBuilder::GetConstInputHandleFromPin(const UEdGraphPin* InPin)
		{
			return GetInputHandleFromPin(InPin);
		}

		Frontend::FOutputHandle FGraphBuilder::GetOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin && ensure(InPin->Direction == EGPD_Output))
			{
				if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					FNodeHandle NodeHandle = EdNode->GetNodeHandle();
					if (NodeHandle->IsValid())
					{
						TArray<FOutputHandle> Outputs = NodeHandle->GetOutputsWithVertexName(InPin->GetName());
						if (ensure(Outputs.Num() == 1))
						{
							return Outputs[0];
						}
					}
				}
			}

			return IOutputController::GetInvalidHandle();
		}

		Frontend::FConstOutputHandle FGraphBuilder::GetConstOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			return GetOutputHandleFromPin(InPin);
		}

		void FGraphBuilder::SynchronizeNodeLocation(FVector2D InLocation, Frontend::FNodeHandle InNodeHandle, UMetasoundEditorGraphNode& InNode)
		{
			InNode.NodePosX = InLocation.X;
			InNode.NodePosY = InLocation.Y;

			FMetasoundFrontendNodeStyle Style = InNodeHandle->GetNodeStyle();
			Style.Display.Locations.FindOrAdd(InNode.NodeGuid) = InLocation;
			InNodeHandle->SetNodeStyle(Style);
		}

		UMetasoundEditorGraphInputNode* FGraphBuilder::AddInputNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			UMetasoundEditorGraph* MetasoundGraph = Cast<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			if (!ensure(MetasoundGraph))
			{
				return nullptr;
			}

			UMetasoundEditorGraphInputNode* NewGraphNode = MetasoundGraph->CreateInputNode(InNodeHandle, bInSelectNewNode);
			if (ensure(NewGraphNode))
			{
				SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

				RebuildNodePins(*NewGraphNode);
				return NewGraphNode;
			}

			return nullptr;
		}

		bool FGraphBuilder::GetPinLiteral(UEdGraphPin& InInputPin, FMetasoundFrontendLiteral& OutDefaultLiteral)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			FInputHandle InputHandle = GetInputHandleFromPin(&InInputPin);
			if (!ensure(InputHandle->IsValid()))
			{
				return false;
			}

			const FString& InStringValue = InInputPin.DefaultValue;
			const FName TypeName = InputHandle->GetDataType();
			const FEditorDataType DataType = EditorModule.FindDataType(TypeName);
			switch (DataType.RegistryInfo.PreferredLiteralType)
			{
				case ELiteralType::Boolean:
				{
					OutDefaultLiteral.Set(FCString::ToBool(*InStringValue));
				}
				break;

				case ELiteralType::Float:
				{
					OutDefaultLiteral.Set(FCString::Atof(*InStringValue));
				}
				break;

				case ELiteralType::Integer:
				{
					OutDefaultLiteral.Set(FCString::Atoi(*InStringValue));
				}
				break;

				case ELiteralType::String:
				{
					OutDefaultLiteral.Set(InStringValue);
				}
				break;

				case ELiteralType::UObjectProxy:
				{
					bool bObjectFound = false;
					if (!InInputPin.DefaultValue.IsEmpty())
					{
						FMetasoundFrontendRegistryContainer* FrontendRegistry = FMetasoundFrontendRegistryContainer::Get();
						check(FrontendRegistry);

						if (UClass* Class = FrontendRegistry->GetLiteralUClassForDataType(TypeName))
						{
							FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

							const FString ClassName = Class->GetName();

							// Remove class prefix if included in default value path
							FString ObjectPath = InInputPin.DefaultValue;
							ObjectPath.RemoveFromStart(ClassName + TEXT(" "));

							FARFilter Filter;
							Filter.bRecursiveClasses = false;
							Filter.ObjectPaths.Add(*ObjectPath);
							Filter.ClassNames.Add(Class->GetFName());

							TArray<FAssetData> AssetData;
							AssetRegistryModule.Get().GetAssets(Filter, AssetData);
							if (!AssetData.IsEmpty())
							{
								OutDefaultLiteral.Set(AssetData[0].GetAsset());
								bObjectFound = true;
							}
						}
					}
					
					if (!bObjectFound)
					{
						OutDefaultLiteral.Set(static_cast<UObject*>(nullptr));
					}
				}
				break;

				case ELiteralType::BooleanArray:
				{
					OutDefaultLiteral.Set(TArray<bool>());
				}
				break;

				case ELiteralType::FloatArray:
				{
					OutDefaultLiteral.Set(TArray<float>());
				}
				break;

				case ELiteralType::IntegerArray:
				{
					OutDefaultLiteral.Set(TArray<int32>());
				}
				break;

				case ELiteralType::NoneArray:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefaultArray());
				}
				break;

				case ELiteralType::StringArray:
				{
					OutDefaultLiteral.Set(TArray<FString>());
				}
				break;

				case ELiteralType::UObjectProxyArray:
				{
					OutDefaultLiteral.Set(TArray<UObject*>());
				}
				break;

				case ELiteralType::None:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefault());
				}
				break;

				case ELiteralType::Invalid:
				default:
				{
					static_assert(static_cast<int32>(ELiteralType::COUNT) == 13, "Possible missing ELiteralType case coverage.");
					ensureMsgf(false, TEXT("Failed to set input node default: Literal type not supported"));
					return false;
				}
				break;
			}

			return true;
		}

		Frontend::FNodeHandle FGraphBuilder::AddNodeHandle(UObject& InMetasound, UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
			if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(&InGraphNode))
			{
				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Output))
				{
					UMetasoundEditorGraphInput* Input = InputNode->Input;
					if (ensure(Input))
					{
						NodeHandle = FGraphBuilder::AddInputNodeHandle(InMetasound, Input->TypeName, InGraphNode.GetTooltipText());
						NodeHandle->SetDisplayName(FText::FromString(Pin->GetName()));
					}
				}
			}

			else if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(&InGraphNode))
			{
				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Input))
				{
					UMetasoundEditorGraphOutput* Output = OutputNode->Output;
					if (ensure(Output))
					{
						NodeHandle = FGraphBuilder::AddOutputNodeHandle(InMetasound, Output->TypeName, InGraphNode.GetTooltipText());
						NodeHandle->SetDisplayName(FText::FromString(Pin->GetName()));
					}
				}
			}

			else if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(&InGraphNode))
			{
				FMetasoundFrontendClass FrontendClass;
				bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(ExternalNode->ClassName.ToNodeClassName(), FrontendClass);
				if (ensure(bDidFindClassWithName))
				{
					FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
					check(MetasoundAsset);

					Frontend::FNodeHandle NewNode = MetasoundAsset->GetRootGraphHandle()->AddNode(FrontendClass.Metadata);
					ExternalNode->SetNodeID(NewNode->GetID());

					NodeHandle = NewNode;
				}
			}

			if (NodeHandle->IsValid())
			{
				FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
				Style.Display.Locations.Add(InGraphNode.NodeGuid, FVector2D(InGraphNode.NodePosX, InGraphNode.NodePosY));
				NodeHandle->SetNodeStyle(Style);
			}

			return NodeHandle;
		}

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetasound, const FName InTypeName, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			const FGuid VertexID = FGuid::NewGuid();

			FMetasoundFrontendClassInput Description;

			// The user currently never interfaces with the name, only the display name,
			// so just set to VertexID to make unique, avoid confusion, and eventually
			// this field will most likely go away (or become the display name).
			Description.Name = VertexID.ToString();

			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = VertexID;

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddInputVertex(Description);
			if (ensure(NodeHandle->IsValid()))
			{
				const FText DisplayName = FGraphBuilder::GenerateUniqueInputDisplayName(InMetasound);
				GraphHandle->SetInputDisplayName(Description.Name, DisplayName);

				if (InDefaultValue)
				{
					GraphHandle->SetDefaultInput(VertexID, *InDefaultValue);
				}
				else
				{
					GraphHandle->SetDefaultInputToDefaultLiteralOfType(VertexID);
				}
			}

			return NodeHandle;
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetasound, const FName InTypeName, const FText& InToolTip)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			const FGuid VertexID = FGuid::NewGuid();

			FMetasoundFrontendClassOutput Description;

			// The user currently never interfaces with the name, only the display name,
			// so just set to VertexID to make unique, avoid confusion, and eventually
			// this field will most likely go away (or become the display name).
			Description.Name = VertexID.ToString();

			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = VertexID;

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddOutputVertex(Description);

			const FText DisplayName = FGraphBuilder::GenerateUniqueOutputDisplayName(InMetasound);
			GraphHandle->SetOutputDisplayName(Description.Name, DisplayName);

			return NodeHandle;
		}

		UMetasoundEditorGraphNode* FGraphBuilder::AddNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			switch (InNodeHandle->GetClassMetadata().Type)
			{
				case EMetasoundFrontendClassType::Input:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddInputNode(InMetasound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::External:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddExternalNode(InMetasound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Output:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddOutputNode(InMetasound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Invalid:
				case EMetasoundFrontendClassType::Graph:
				case EMetasoundFrontendClassType::Variable:
				default:
				{
					checkNoEntry();
					static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing FMetasoundFrontendClassType case coverage");
				}
				break;
			}

			return nullptr;
		}

		bool FGraphBuilder::ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bInConnectEdPins)
		{
			using namespace Frontend;

			// When true, will recursively call back into this function
			// from the schema if the editor pins are successfully connected
			if (bInConnectEdPins)
			{
				const UEdGraphSchema* Schema = InInputPin.GetSchema();
				if (ensure(Schema))
				{
					return Schema->TryCreateConnection(&InInputPin, &InOutputPin);
				}
				else
				{
					return false;
				}
			}

			FInputHandle InputHandle = GetInputHandleFromPin(&InInputPin);
			FOutputHandle OutputHandle = GetOutputHandleFromPin(&InOutputPin);
			if (!InputHandle->IsValid() || !OutputHandle->IsValid())
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			if (!ensure(InputHandle->Connect(*OutputHandle)))
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			return true;
		}

		void FGraphBuilder::DisconnectPin(UEdGraphPin& InPin, bool bAddLiteralInputs)
		{
			using namespace Editor;
			using namespace Frontend;

			TArray<FInputHandle> InputHandles;
			TArray<UEdGraphPin*> InputPins;

			UObject& Metasound = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode())->GetMetasoundChecked();

			if (InPin.Direction == EGPD_Input)
			{
				FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode())->GetNodeHandle();
				InputHandles = NodeHandle->GetInputsWithVertexName(InPin.GetName());
				InputPins.Add(&InPin);
			}
			else
			{
				check(InPin.Direction == EGPD_Output);
				for (UEdGraphPin* Pin : InPin.LinkedTo)
				{
					FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode())->GetNodeHandle();
					InputHandles.Append(NodeHandle->GetInputsWithVertexName(Pin->GetName()));
					InputPins.Add(Pin);
				}
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				FInputHandle InputHandle = InputHandles[i];
				FConstOutputHandle OutputHandle = InputHandle->GetConnectedOutput();
				const FMetasoundFrontendNodeStyle& Style = OutputHandle->GetOwningNode()->GetNodeStyle();

				InputHandle->Disconnect();

				if (bAddLiteralInputs)
				{
					FNodeHandle NodeHandle = InputHandle->GetOwningNode();
					SynchronizePinLiteral(*InputPins[i]);
				}
			}
		}

		void FGraphBuilder::InitMetaSound(UObject& InMetaSound, const FString& InAuthor, bool bConformToArchetype)
		{
			using namespace Frontend;
			using namespace GraphBuilderPrivate;

			FMetasoundFrontendClassMetadata Metadata;

			// 1. Set default class Metadata
			Metadata.ClassName = FMetasoundFrontendClassName(FName(), *FGuid::NewGuid().ToString(), FName());
			Metadata.Version.Major = 1;
			Metadata.Version.Minor = 0;
			Metadata.DisplayName = FText::FromString(InMetaSound.GetName());
			Metadata.Type = EMetasoundFrontendClassType::Graph;
			Metadata.Author = FText::FromString(InAuthor);

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			MetaSoundAsset->SetMetadata(Metadata);

			// 2. Set default doc version Metadata
			FDocumentHandle DocumentHandle = MetaSoundAsset->GetDocumentHandle();
			FMetasoundFrontendDocumentMetadata DocMetadata = DocumentHandle->GetMetadata();
			DocMetadata.Version.Number = FVersionDocument::GetMaxVersion();
			DocumentHandle->SetMetadata(DocMetadata);

			if (bConformToArchetype)
			{
				MetaSoundAsset->ConformDocumentToArchetype();
			}

			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			FVector2D InputNodeLocation = FVector2D::ZeroVector;
			FVector2D ExternalNodeLocation = InputNodeLocation + NodeLayout::BufferX;
			FVector2D OutputNodeLocation = ExternalNodeLocation + NodeLayout::BufferX;

			TArray<FNodeHandle> NodeHandles = GraphHandle->GetNodes();
			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				const EMetasoundFrontendClassType NodeType = NodeHandle->GetClassMetadata().Type;
				FVector2D NewLocation;
				if (NodeType == EMetasoundFrontendClassType::Input)
				{
					NewLocation = InputNodeLocation;
					InputNodeLocation += NodeLayout::BufferY;
				}
				else if (NodeType == EMetasoundFrontendClassType::Output)
				{
					NewLocation = OutputNodeLocation;
					OutputNodeLocation += NodeLayout::BufferY;
				}
				else
				{
					NewLocation = ExternalNodeLocation;
					ExternalNodeLocation += NodeLayout::BufferY;
				}
				FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
				Style.Display.Locations.Add(FGuid::NewGuid(), NewLocation);
				NodeHandle->SetNodeStyle(Style);
			}

			MetaSoundAsset->RegisterGraphWithFrontend();
		}

		void FGraphBuilder::InitMetaSoundPreset(UObject& InMetaSoundReferenced, UObject& InMetaSoundPreset)
		{
			using namespace Frontend;
			using namespace GraphBuilderPrivate;

			FMetasoundAssetBase* MetaSoundReferencedAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundReferenced);
			check(MetaSoundReferencedAsset);

			FConstGraphHandle ReferencedGraphHandle = MetaSoundReferencedAsset->GetRootGraphHandle();
			if (!ensure(ReferencedGraphHandle->IsValid()))
			{
				return;
			}

			FMetasoundAssetBase* MetaSoundPresetAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundPreset);
			check(MetaSoundPresetAsset);
			FGraphHandle PresetGraphHandle = MetaSoundPresetAsset->GetRootGraphHandle();
			if (!ensure(PresetGraphHandle->IsValid()))
			{
				return;
			}

			// Ensure referenced MetaSound is up-to-date
			check(MetaSoundReferencedAsset);
			FName Name = InMetaSoundReferenced.GetFName();
			FString Path = InMetaSoundReferenced.GetPathName();
			if (Frontend::FVersionDocument(Name, Path).Transform(MetaSoundReferencedAsset->GetDocumentHandle()))
			{
				InMetaSoundReferenced.MarkPackageDirty();
			}

			// Ensure asset is registered with frontend so the class
			// can be queried when reference is added. This may modify
			// registry properties, so must be non-const.
			MetaSoundReferencedAsset->RegisterGraphWithFrontend();

			// Initialize EdGraph, but don't conform to archetype as this would provide the
			// default interface (inputs/outputs), which will be added below when duplicating
			// node's interface to the preset.
			GraphBuilderPrivate::InitializeGraph(InMetaSoundPreset);

			const FMetasoundFrontendArchetype& Archetype = MetaSoundPresetAsset->GetArchetype();

			// 1a. Preset graph topology is considered "read-only", so mark as such
			FMetasoundFrontendGraphStyle Style = PresetGraphHandle->GetGraphStyle();
			Style.bIsGraphEditable = false;
			PresetGraphHandle->SetGraphStyle(Style);

			// 1b. Set archetype accordingly
			const FMetasoundFrontendArchetype& RefArchetype = MetaSoundReferencedAsset->GetArchetype();
			if (ensure(MetaSoundPresetAsset->IsArchetypeSupported(RefArchetype)))
			{
				MetaSoundPresetAsset->ConformDocumentToArchetype();
			}

			// 2. Generate referenced inputs & outputs
			FVector2D InputNodeLocation = FVector2D::ZeroVector;
			FVector2D ExternalNodeLocation = InputNodeLocation + NodeLayout::BufferX;
			FVector2D OutputNodeLocation = ExternalNodeLocation + NodeLayout::BufferX;

			TMap<FString, FString> PresetInputToReferenceInputMap;
			TMap<FString, FString> PresetOutputToReferenceOutputMap;

			ReferencedGraphHandle->IterateConstNodes([&](FConstNodeHandle RefGraphInputNode)
			{
				// Required inputs are added via the prior step (archetype conformation).
				// For these, find the corrisponding input & create an editor node only.
				const bool bIsRequired = RefGraphInputNode->IsRequired(Archetype);

				const FText& DisplayName = RefGraphInputNode->GetDisplayName();
				const FText& Description = RefGraphInputNode->GetDescription();

				// Should only ever be one
				ensure(RefGraphInputNode->GetNumInputs() == 1);
				RefGraphInputNode->IterateConstInputs([&](FConstInputHandle RefGraphInputHandle)
				{
					FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
					if (bIsRequired)
					{
						NodeHandle = PresetGraphHandle->GetInputNodeWithName(RefGraphInputHandle->GetName());
					}
					else
					{
						FGuid InputID = ReferencedGraphHandle->GetVertexIDForInputVertex(RefGraphInputHandle->GetName());
						const FMetasoundFrontendLiteral DefaultLiteral = ReferencedGraphHandle->GetDefaultInput(InputID);

						const FName DataTypeName = RefGraphInputHandle->GetDataType();
						NodeHandle = AddInputNodeHandle(InMetaSoundPreset, DataTypeName, Description, &DefaultLiteral);
						NodeHandle->SetDisplayName(DisplayName);
					}

					if (ensure(NodeHandle->IsValid()))
					{
						UMetasoundEditorGraphInputNode* InputNode = AddInputNode(InMetaSoundPreset, NodeHandle, InputNodeLocation, false /* bSelectNewNode */);
						if (ensure(InputNode))
						{
							PresetInputToReferenceInputMap.Add(NodeHandle->GetNodeName(), RefGraphInputHandle->GetName());
						}
						InputNodeLocation += NodeLayout::BufferY;
					}
				});
			},
			EMetasoundFrontendClassType::Input);

			ReferencedGraphHandle->IterateConstNodes([&](FConstNodeHandle RefGraphOutputNode)
			{
				// Required inputs are added via the prior step (archetype conformation).
				// For these, find the corrisponding input & create an editor node only.
				const bool bIsRequired = RefGraphOutputNode->IsRequired(Archetype);

				const FText& DisplayName = RefGraphOutputNode->GetDisplayName();
				const FText& Description = RefGraphOutputNode->GetDescription();

				// Should only ever be one
				ensure(RefGraphOutputNode->GetNumOutputs() == 1);
				RefGraphOutputNode->IterateConstOutputs([&](FConstOutputHandle RefGraphOutputHandle)
				{
					Frontend::FNodeHandle NodeHandle = Frontend::INodeController::GetInvalidHandle();
					if (bIsRequired)
					{
						NodeHandle = PresetGraphHandle->GetOutputNodeWithName(RefGraphOutputHandle->GetName());
					}
					else
					{
						const FName DataTypeName = RefGraphOutputHandle->GetDataType();
						NodeHandle = AddOutputNodeHandle(InMetaSoundPreset, DataTypeName, Description);
						NodeHandle->SetDisplayName(DisplayName);
					}

					if (ensure(NodeHandle->IsValid()))
					{
						UMetasoundEditorGraphOutputNode* OutputNode = AddOutputNode(InMetaSoundPreset, NodeHandle, OutputNodeLocation, false /* bSelectNewNode */);
						if (ensure(OutputNode))
						{
							PresetOutputToReferenceOutputMap.Add(OutputNode->GetNodeHandle()->GetNodeName(), RefGraphOutputHandle->GetName());
						}
						OutputNodeLocation += NodeLayout::BufferY;
					}
				});
			},
			EMetasoundFrontendClassType::Output);

			// 3. Generate a referencing node to the given referenced MetaSound.
			FMetasoundFrontendClassMetadata ReferencedClassMetadata = ReferencedGraphHandle->GetGraphMetadata();

			// Swap type on look-up as it will be referenced as an externally defined class relative to the new Preset asset
			ReferencedClassMetadata.Type = EMetasoundFrontendClassType::External;
			UMetasoundEditorGraphExternalNode* ReferencedNode = AddExternalNode(InMetaSoundPreset, ReferencedClassMetadata, ExternalNodeLocation, false /* bSelectNewNode */);
			FNodeHandle ReferencedNodeHandle = ReferencedNode->GetNodeHandle();

			// 4. Connect preset's respective inputs & outputs to generated referencing node.
			PresetGraphHandle->IterateNodes([&](FNodeHandle InputNode)
			{
				const FString& Name = InputNode->GetNodeName();

				// Should only ever be one
				ensure(InputNode->GetNumOutputs() == 1);
				InputNode->IterateOutputs([&](FOutputHandle OutputHandle)
				{
					const FString& InputName = PresetInputToReferenceInputMap.FindChecked(Name);
					const TArray<FInputHandle> ReferenceInputs = ReferencedNodeHandle->GetInputsWithVertexName(InputName);
					if (ensure(ReferenceInputs.Num() == 1))
					{
						ensure(ReferenceInputs[0]->Connect(*OutputHandle));
					}
				});
			},
			EMetasoundFrontendClassType::Input);

			PresetGraphHandle->IterateNodes([&](FNodeHandle OutputNode)
			{
				const FString& Name = OutputNode->GetNodeName();

				// Should only ever be one
				ensure(OutputNode->GetNumInputs() == 1);
				OutputNode->IterateInputs([&](FInputHandle InputHandle)
				{
					const FString& OutputName = PresetOutputToReferenceOutputMap.FindChecked(Name);
					const TArray<FOutputHandle> ReferenceInputs = ReferencedNodeHandle->GetOutputsWithVertexName(OutputName);
					if (ensure(ReferenceInputs.Num() == 1))
					{
						ensure(InputHandle->Connect(*ReferenceInputs[0]));
					}
				});
			},
			EMetasoundFrontendClassType::Output);
		}

		void FGraphBuilder::DeleteVariableNodeHandle(UMetasoundEditorGraphVariable& InVariable)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = InVariable.GetNodeHandle();
			TArray<UMetasoundEditorGraphNode*> Nodes = InVariable.GetNodes();
			for (UMetasoundEditorGraphNode* Node : Nodes)
			{
				if (ensure(Node))
				{
					// Remove the give node's location from the frontend node
					FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
					Style.Display.Locations.Remove(Node->NodeGuid);
					NodeHandle->SetNodeStyle(Style);

					FGraphBuilder::DeleteNode(*Node);
				}
			}

			const FString NodeName = NodeHandle->GetNodeName();
			const FText NodeDisplayName = NodeHandle->GetDisplayName();
			FGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
			GraphHandle->RemoveNode(*NodeHandle);
		}

		bool FGraphBuilder::DeleteNode(UEdGraphNode& InNode)
		{
			using namespace Frontend;

			if (!InNode.CanUserDeleteNode())
			{
				return false;
			}

			bool bWasErroredNode = InNode.ErrorType == EMessageSeverity::Error;

			// If node isn't a MetasoundEditorGraphNode, just remove and return (ex. comment nodes)
			UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(&InNode);
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InNode.GetGraph());
			if (!Node)
			{
				Graph->RemoveNode(&InNode);
				return true;
			}

			FNodeHandle NodeHandle = Node->GetNodeHandle();
			if (!ensure(NodeHandle->IsValid()))
			{
				return false;
			}

			// Remove connects only to pins associated with this EdGraph node
			// only (Iterate pins and not Frontend representation to preserve
			// other input/output EditorGraph reference node associations)
			Node->IteratePins([](UEdGraphPin& Pin, int32 Index)
			{
				// Only add literal inputs for output pins as adding when disconnecting
				// inputs would immediately orphan them on EditorGraph node removal below.
				const bool bAddLiteralInputs = Pin.Direction == EGPD_Output;
				FGraphBuilder::DisconnectPin(Pin, bAddLiteralInputs);
			});

			Frontend::FGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
			if (GraphHandle->IsValid())
			{
				switch (NodeHandle->GetClassMetadata().Type)
				{
					case EMetasoundFrontendClassType::Output:
					case EMetasoundFrontendClassType::Input:
					{
						// NodeHandle does not get removed in these cases as EdGraph Inputs/Outputs
						// merely reference their respective types set on the MetasoundGraph. It must
						// be removed from the location display data however for graph sync reasons.
						FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
						Style.Display.Locations.Remove(InNode.NodeGuid);
						NodeHandle->SetNodeStyle(Style);
					}
					break;

					case EMetasoundFrontendClassType::Graph:
					case EMetasoundFrontendClassType::Variable:
					case EMetasoundFrontendClassType::External:
					default:
					{
						static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing MetasoundFrontendClassType switch case coverage.");

						if (ensure(GraphHandle->RemoveNode(*NodeHandle)))
						{
							GraphHandle->GetOwningDocument()->SynchronizeDependencies();
						}
					}
					break;
				}
			}

			const bool bSuccess = ensure(Graph->RemoveNode(&InNode));

			// Sync the graph after nodes containing errors are deleted to ensure that
			// the graph is not malformed once all errors are addressed by the user.
			if (bSuccess && bWasErroredNode)
			{
				SynchronizeGraph(Graph->GetMetasoundChecked());
			}

			return bSuccess;
		}

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;
		
			for (int32 i = InGraphNode.Pins.Num() - 1; i >= 0; i--)
			{
				InGraphNode.RemovePin(InGraphNode.Pins[i]);
			}

			// TODO: Make this a utility in Frontend (ClearInputLiterals())
			FNodeHandle NodeHandle = InGraphNode.GetNodeHandle();
			TArray<FInputHandle> Inputs = NodeHandle->GetInputs();
			for (FInputHandle& Input : Inputs)
			{
				NodeHandle->ClearInputLiteral(Input->GetID());
			}

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != NodeHandle->GetClassMetadata().Type)
			{
				TArray<FInputHandle> InputHandles = NodeHandle->GetInputs();
				InputHandles = NodeHandle->GetInputStyle().SortDefaults(InputHandles);
				for (const FInputHandle& InputHandle : InputHandles)
				{
					AddPinToNode(InGraphNode, InputHandle);
				}
			}

			// Only add output pins of the node is not an output node. Output nodes
			// have their output pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Output != NodeHandle->GetClassMetadata().Type)
			{
				TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
				OutputHandles = NodeHandle->GetOutputStyle().SortDefaults(OutputHandles);
				for (const FOutputHandle& OutputHandle : OutputHandles)
				{
					AddPinToNode(InGraphNode, OutputHandle);
				}
			}
		}

		void FGraphBuilder::RefreshPinMetadata(UEdGraphPin& InPin, const FMetasoundFrontendVertexMetadata& InMetadata)
		{
			InPin.PinToolTip = InMetadata.Description.ToString();
			InPin.bAdvancedView = InMetadata.bIsAdvancedDisplay;
			if (InPin.bAdvancedView)
			{
				UEdGraphNode* OwningNode = InPin.GetOwningNode();
				check(OwningNode);
				if (OwningNode->AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
				{
					OwningNode->AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
				}
			}
		}

		bool FGraphBuilder::IsMatchingInputHandleAndPin(const Frontend::FInputHandle& InInputHandle, const UEdGraphPin& InEditorPin)
		{
			Frontend::FInputHandle PinInputHandle = GetInputHandleFromPin(&InEditorPin);
			if (PinInputHandle->GetID() == InInputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		bool FGraphBuilder::IsMatchingOutputHandleAndPin(const Frontend::FOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin)
		{
			Frontend::FOutputHandle PinOutputHandle = GetOutputHandleFromPin(&InEditorPin);
			if (PinOutputHandle->GetID() == InOutputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		void FGraphBuilder::DepthFirstTraversal(UEdGraphNode* InInitialNode, FDepthFirstVisitFunction InVisitFunction)
		{
			// Non recursive depth first traversal.
			TArray<UEdGraphNode*> Stack({InInitialNode});
			TSet<UEdGraphNode*> Visited;

			while (Stack.Num() > 0)
			{
				UEdGraphNode* CurrentNode = Stack.Pop();
				if (Visited.Contains(CurrentNode))
				{
					// Do not revisit a node that has already been visited. 
					continue;
				}

				TArray<UEdGraphNode*> Children = InVisitFunction(CurrentNode).Array();
				Stack.Append(Children);

				Visited.Add(CurrentNode);
			}
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle InInputHandle)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			const FEdGraphPinType PinType = EditorModule.FindDataType(InInputHandle->GetDataType()).PinType;

			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, FName(*InInputHandle->GetName()));
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InInputHandle->GetMetadata());

				FNodeHandle NodeHandle = InInputHandle->GetOwningNode();
				SynchronizePinLiteral(*NewPin);
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle InOutputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			FEdGraphPinType	PinType = EditorModule.FindDataType(InOutputHandle->GetDataType()).PinType;

			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, FName(*InOutputHandle->GetName()));
			if (ensure(NewPin))
			{
				NewPin->PinToolTip = InOutputHandle->GetTooltip().ToString();
				NewPin->bAdvancedView = InOutputHandle->GetMetadata().bIsAdvancedDisplay;
				if (NewPin->bAdvancedView)
				{
					if (InEditorNode.AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
					{
						InEditorNode.AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
					}
				}
			}

			return NewPin;
		}

		bool FGraphBuilder::SynchronizeConnections(UObject& InMetasound)
		{
			using namespace Frontend;

			bool bIsGraphDirty = false;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());


			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			TMap<FGuid, TArray<UMetasoundEditorGraphNode*>> EditorNodesByFrontendID;
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				EditorNodesByFrontendID.FindOrAdd(EditorNode->GetNodeID()).Add(EditorNode);
			}

			// Iterate through all nodes in metasound editor graph and synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bool bIsNodeDirty = false;

				FNodeHandle Node = EditorNode->GetNodeHandle();

				if (EMetasoundFrontendClassType::Input == Node->GetClassMetadata().Type)
				{
					// Skip this node if it is an input node. Input pins on input 
					// nodes are not connected internal to the graph.
					continue;
				}

				TArray<UEdGraphPin*> Pins = EditorNode->GetAllPins();
				TArray<FInputHandle> NodeInputs = Node->GetInputs();

				for (FInputHandle& NodeInput : NodeInputs)
				{
					auto IsMatchingInputPin = [&](const UEdGraphPin* Pin) -> bool
					{
						return IsMatchingInputHandleAndPin(NodeInput, *Pin);
					};

					UEdGraphPin* MatchingPin = nullptr;
					if (UEdGraphPin** DoublePointer = Pins.FindByPredicate(IsMatchingInputPin))
					{
						MatchingPin = *DoublePointer;
					}

					if (!ensure(MatchingPin))
					{
						continue;
					}

					// Remove pin so it isn't used twice.
					Pins.Remove(MatchingPin);

					FOutputHandle OutputHandle = NodeInput->GetConnectedOutput();
					if (OutputHandle->IsValid())
					{
						bool bAddLink = false;

						if (MatchingPin->LinkedTo.IsEmpty())
						{
							// No link currently exists. Add the appropriate link.
							bAddLink = true;
						}
						else if (!IsMatchingOutputHandleAndPin(OutputHandle, *MatchingPin->LinkedTo[0]))
						{
							// The wrong link exists.
							MatchingPin->BreakAllPinLinks();
							bAddLink = true;
						}

						if (bAddLink)
						{
							const FGuid NodeID = OutputHandle->GetOwningNodeID();
							TArray<UMetasoundEditorGraphNode*>* OutputEditorNode = EditorNodesByFrontendID.Find(NodeID);
							if (ensure(OutputEditorNode))
							{
								if (ensure(!OutputEditorNode->IsEmpty()))
								{
									UEdGraphPin* OutputPin = (*OutputEditorNode)[0]->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
									const FText& OwningNodeName = NodeInput->GetOwningNode()->GetDisplayName();
									UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Connection: Linking Pin '%s' to '%s'"), *OwningNodeName.ToString(), *MatchingPin->GetName(), *OutputPin->GetName());
									MatchingPin->MakeLinkTo(OutputPin);
									bIsNodeDirty = true;
								}
							}
						}
					}
					else
					{
						// No link should exist.
						if (!MatchingPin->LinkedTo.IsEmpty())
						{
							MatchingPin->BreakAllPinLinks();
							const FText& OwningNodeName = NodeInput->GetOwningNode()->GetDisplayName();
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Connection: Breaking all pin links to '%s'"), *OwningNodeName.ToString(), *NodeInput->GetDisplayName().ToString());
							bIsNodeDirty = true;
						}
					}

					SynchronizePinLiteral(*MatchingPin);
				}

				bIsGraphDirty |= bIsNodeDirty;
			}

			return bIsGraphDirty;
		}

		bool FGraphBuilder::SynchronizeGraph(UObject& InMetasound)
		{
			using namespace Frontend;

			GraphBuilderPrivate::InitializeGraph(InMetasound);

			bool bIsEditorGraphDirty = SynchronizeVariables(InMetasound);

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			// Get all external nodes from Frontend graph.  Input and output references will only be added/synchronized
			// if required when synchronizing connections (as they are not required to inhabit editor graph).
			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();

			// Get all editor nodes from editor graph (some nodes on graph may *NOT* be metasound ed nodes,
			// such as comment boxes, etc, so just get nodes of class UMetasoundEditorGraph).
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			// Do not synchronize with errors present as the graph is expected to be malformed.
			for (UMetasoundEditorGraphNode* Node : EditorNodes)
			{
				if (Node->ErrorType == EMessageSeverity::Error)
				{
					return true;
				}
			}

			TMap<FGuid, UMetasoundEditorGraphNode*> EditorNodesByEdNodeGuid;
			for (UMetasoundEditorGraphNode* Node : EditorNodes)
			{
				EditorNodesByEdNodeGuid.Add(Node->NodeGuid, Node);
			}

			// Find existing array of editor nodes associated with Frontend node
			struct FAssociatedNodes
			{
				TArray<UMetasoundEditorGraphNode*> EditorNodes;
				FNodeHandle Node = Metasound::Frontend::INodeController::GetInvalidHandle();
			};
			TMap<FGuid, FAssociatedNodes> AssociatedNodes;

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = FrontendNodes.Num() - 1; i >= 0; i--)
			{
				FNodeHandle Node = FrontendNodes[i];
				auto IsEditorNodeWithSameNodeID = [&](const UMetasoundEditorGraphNode* InEditorNode)
				{
					return InEditorNode->GetNodeID() == Node->GetID();
				};

				bool bFoundEditorNode = false;
				for (int32 j = EditorNodes.Num() - 1; j >= 0; --j)
				{
					UMetasoundEditorGraphNode* EditorNode = EditorNodes[j];
					if (EditorNode->GetNodeID() == Node->GetID())
					{
						bFoundEditorNode = true;
						FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
						if (AssociatedNodeData.Node->IsValid())
						{
							ensure(AssociatedNodeData.Node == Node);
						}
						else
						{
							AssociatedNodeData.Node = Node;
						}

						AssociatedNodeData.EditorNodes.Add(EditorNode);
						EditorNodes.RemoveAtSwap(j, 1, false /* bAllowShrinking */);
					}
				}

				if (bFoundEditorNode)
				{
					FrontendNodes.RemoveAtSwap(i, 1, false /* bAllowShrinking */);
				}
			}

			// FrontendNodes contains nodes which need to be added to the editor graph.
			// EditorNodes contains nodes that need to be removed from the editor graph.
			// AssociatedNodes contains pairs which we have to check have synchronized pins

			// Add and remove nodes first in order to make sure correct editor nodes
			// exist before attempting to synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bIsEditorGraphDirty |= EditorGraph->RemoveNode(EditorNode);
			}

			// Add missing editor nodes marked as visible.
			for (FNodeHandle Node : FrontendNodes)
			{
				const FMetasoundFrontendNodeStyle& CurrentStyle = Node->GetNodeStyle();
				if (CurrentStyle.Display.Locations.IsEmpty())
				{
					continue;
				}

				FMetasoundFrontendNodeStyle NewStyle = CurrentStyle;
				bIsEditorGraphDirty = true;

				TArray<UMetasoundEditorGraphNode*> AddedNodes;
				for (const TPair<FGuid, FVector2D>& Location : NewStyle.Display.Locations)
				{
					UMetasoundEditorGraphNode* NewNode = AddNode(InMetasound, Node, Location.Value, false /* bInSelectNewNode */);
					if (ensure(NewNode))
					{
						FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
						if (AssociatedNodeData.Node->IsValid())
						{
							ensure(AssociatedNodeData.Node == Node);
						}
						else
						{
							AssociatedNodeData.Node = Node;
						}

						AddedNodes.Add(NewNode);
						AssociatedNodeData.EditorNodes.Add(NewNode);
					}
				}

				NewStyle.Display.Locations.Reset();
				for (UMetasoundEditorGraphNode* EditorNode : AddedNodes)
				{
					NewStyle.Display.Locations.Add(EditorNode->NodeGuid, FVector2D(EditorNode->NodePosX, EditorNode->NodePosY));
				}
				Node->SetNodeStyle(NewStyle);
			}

			// Synchronize pins on node associations.
			for (const TPair<FGuid, FAssociatedNodes>& IdNodePair : AssociatedNodes)
			{
				for (UMetasoundEditorGraphNode* EditorNode : IdNodePair.Value.EditorNodes)
				{
					bIsEditorGraphDirty |= SynchronizeNodePins(*EditorNode, IdNodePair.Value.Node);
				}
			}

			// Synchronize connections.
			bIsEditorGraphDirty |= SynchronizeConnections(InMetasound);
			return bIsEditorGraphDirty;
		}

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode, bool bRemoveUnusedPins, bool bLogChanges)
		{
			bool bIsNodeDirty = false;

			TArray<Frontend::FInputHandle> InputHandles = InNode->GetInputs();
			TArray<Frontend::FOutputHandle> OutputHandles = InNode->GetOutputs();
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;

			// Filter out pins which are not paired.
			for (int32 i = EditorPins.Num() - 1; i >= 0; i--)
			{
				UEdGraphPin* Pin = EditorPins[i];

				auto IsMatchingInputHandle = [&](const Frontend::FInputHandle& InputHandle) -> bool
				{
					return IsMatchingInputHandleAndPin(InputHandle, *Pin);
				};

				auto IsMatchingOutputHandle = [&](const Frontend::FOutputHandle& OutputHandle) -> bool
				{
					return IsMatchingOutputHandleAndPin(OutputHandle, *Pin);
				};

				switch (Pin->Direction)
				{
					case EEdGraphPinDirection::EGPD_Input:
					{
						int32 MatchingInputIndex = InputHandles.FindLastByPredicate(IsMatchingInputHandle);
						if (INDEX_NONE != MatchingInputIndex)
						{
							InputHandles.RemoveAtSwap(MatchingInputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;

					case EEdGraphPinDirection::EGPD_Output:
					{
						int32 MatchingOutputIndex = OutputHandles.FindLastByPredicate(IsMatchingOutputHandle);
						if (INDEX_NONE != MatchingOutputIndex)
						{
							OutputHandles.RemoveAtSwap(MatchingOutputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;
				}
			}

			// Remove any unused editor pins.
			if (bRemoveUnusedPins)
			{
				bIsNodeDirty |= !EditorPins.IsEmpty();
				for (UEdGraphPin* Pin : EditorPins)
				{
					if (bLogChanges)
					{
						UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Removing Excess Editor Pin '%s'"), *InNode->GetDisplayName().ToString(), *Pin->GetName());
					}
					InEditorNode.RemovePin(Pin);
				}
			}

			const EMetasoundFrontendClassType ClassType = InNode->GetClassMetadata().Type;

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != ClassType)
			{
				if (!InputHandles.IsEmpty())
				{
					bIsNodeDirty = true;
					InputHandles = InNode->GetInputStyle().SortDefaults(InputHandles);
					for (Frontend::FInputHandle& InputHandle : InputHandles)
					{
						if (bLogChanges)
						{
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Input Pin '%s'"), *InNode->GetDisplayName().ToString(), *InputHandle->GetDisplayName().ToString());
						}
						AddPinToNode(InEditorNode, InputHandle);
					}
				}
			}

			// Only add output pins of the node is not an output node. Output nodes
			// have their output pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Output != ClassType)
			{
				if (!OutputHandles.IsEmpty())
				{
					bIsNodeDirty = true;
					OutputHandles = InNode->GetOutputStyle().SortDefaults(OutputHandles);
					for (Frontend::FOutputHandle& OutputHandle : OutputHandles)
					{
						if (bLogChanges)
						{
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Output Pin '%s'"), *InNode->GetDisplayName().ToString(), *OutputHandle->GetDisplayName().ToString());
						}
						AddPinToNode(InEditorNode, OutputHandle);
					}
				}
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizePinLiteral(UEdGraphPin& InPin)
		{
			using namespace Metasound::Frontend;

			if (!ensure(InPin.Direction == EGPD_Input))
			{
				return false;
			}

			const FString OldValue = InPin.DefaultValue;

			FInputHandle InputHandle = GetInputHandleFromPin(&InPin);
			if (const FMetasoundFrontendLiteral* NodeDefaultLiteral = InputHandle->GetLiteral())
			{
				InPin.DefaultValue = NodeDefaultLiteral->ToString();
				return OldValue != InPin.DefaultValue;
			}

			if (const FMetasoundFrontendLiteral* ClassDefaultLiteral = InputHandle->GetClassDefaultLiteral())
			{
				InPin.DefaultValue = ClassDefaultLiteral->ToString();
				return OldValue != InPin.DefaultValue;
			}

			FMetasoundFrontendLiteral DefaultLiteral;
			DefaultLiteral.SetFromLiteral(Frontend::GetDefaultParamForDataType(InputHandle->GetDataType()));
			InPin.DefaultValue = DefaultLiteral.ToString();
			return OldValue != InPin.DefaultValue;
		}

		bool FGraphBuilder::SynchronizeVariables(UObject& InMetasound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			TSet<UMetasoundEditorGraphInput*> Inputs;
			TSet<UMetasoundEditorGraphOutput*> Outputs;

			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphInput* Input = Graph->FindInput(NodeHandle->GetID()))
				{
					Inputs.Add(Input);
					return;
				}

				if (!ensure(NodeHandle->GetNumInputs() == 1))
				{
					return;
				}

				Inputs.Add(Graph->FindOrAddInput(NodeHandle));
				UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Inputs: Added missing input '%s'."), *NodeHandle->GetDisplayName().ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Input);

			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(NodeHandle->GetID()))
				{
					Outputs.Add(Output);
					return;
				}
				if (!ensure(NodeHandle->GetNumOutputs() == 1))
				{
					return;
				}

				Outputs.Add(Graph->FindOrAddOutput(NodeHandle));
				UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Outputs: Added missing output '%s'."), *NodeHandle->GetDisplayName().ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Output);

			TArray<UMetasoundEditorGraphVariable*> ToRemove;
			Graph->IterateInputs([&](UMetasoundEditorGraphInput& Input)
			{
				if (!Inputs.Contains(&Input))
				{
					UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Inputs: Removing stale input '%s'."), *Input.GetName());
					ToRemove.Add(&Input);
				}
			});
			Graph->IterateOutputs([&](UMetasoundEditorGraphOutput& Output)
			{
				if (!Outputs.Contains(&Output))
				{
					UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Outputs: Removing stale output '%s'."), *Output.GetName());
					ToRemove.Add(&Output);
				}
			});

			bIsEditorGraphDirty |= !ToRemove.IsEmpty();
			for (UMetasoundEditorGraphVariable* Variable : ToRemove)
			{
				Graph->RemoveVariable(*Variable);
			}

			return bIsEditorGraphDirty;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
