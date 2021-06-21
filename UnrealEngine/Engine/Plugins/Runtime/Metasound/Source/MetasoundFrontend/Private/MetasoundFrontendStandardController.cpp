// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendStandardController.h"

#include "Algo/ForEach.h"
#include "Algo/NoneOf.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSubgraphNodeController.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundOperatorBuilder.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "Serialization/MemoryReader.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontendStandardController"

namespace Metasound
{
	namespace Frontend
	{
		namespace FrontendControllerIntrinsics
		{
			// utility function for returning invalid values. If an invalid value type
			// needs special construction, this template can be specialized. 
			template<typename ValueType>
			ValueType GetInvalidValue()
			{
				ValueType InvalidValue;
				return InvalidValue;
			}

			// Invalid value specialization for int32
			template<>
			int32 GetInvalidValue<int32>() { return INDEX_NONE; }

			// Invalid value specialization for EMetasoundFrontendClassType
			template<>
			EMetasoundFrontendClassType GetInvalidValue<EMetasoundFrontendClassType>() { return EMetasoundFrontendClassType::Invalid; }

			// Invalid value specialization for FText
			template<>
			FText GetInvalidValue<FText>() { return FText::GetEmpty(); }

			template<typename ValueType>
			const ValueType& GetInvalidValueConstRef()
			{
				static const ValueType Value = GetInvalidValue<ValueType>();
				return Value;
			}

		}


		FDocumentAccess IDocumentAccessor::GetSharedAccess(IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}

		FConstDocumentAccess IDocumentAccessor::GetSharedAccess(const IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}


		//
		// FBaseOutputController
		//
		FBaseOutputController::FBaseOutputController(const FBaseOutputController::FInitParams& InParams)
		: ID(InParams.ID)
		, NodeVertexPtr(InParams.NodeVertexPtr)
		, ClassOutputPtr(InParams.ClassOutputPtr)
		, GraphPtr(InParams.GraphPtr)
		, OwningNode(InParams.OwningNode)
		{
		}

		bool FBaseOutputController::IsValid() const
		{
			return OwningNode->IsValid() && NodeVertexPtr.IsValid() && GraphPtr.IsValid();
		}

		FGuid FBaseOutputController::GetID() const
		{
			return ID;
		}

		const FName& FBaseOutputController::GetDataType() const
		{
			if (NodeVertexPtr.IsValid())
			{
				return NodeVertexPtr->TypeName;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FName>();
		}

		const FString& FBaseOutputController::GetName() const
		{
			if (NodeVertexPtr.IsValid())
			{
				return NodeVertexPtr->Name;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		FGuid FBaseOutputController::GetOwningNodeID() const
		{
			return OwningNode->GetID();
		}

		FNodeHandle FBaseOutputController::GetOwningNode()
		{
			return OwningNode;
		}

		FConstNodeHandle FBaseOutputController::GetOwningNode() const
		{
			return OwningNode;
		}

		bool FBaseOutputController::IsConnected() const 
		{
			return (FindEdges().Num() > 0);
		}

		TArray<FInputHandle> FBaseOutputController::GetConnectedInputs() 
		{
			TArray<FInputHandle> Inputs;

			// Create output handle from output node.
			FGraphHandle Graph = OwningNode->GetOwningGraph();

			for (const FMetasoundFrontendEdge& Edge : FindEdges())
			{
				FNodeHandle InputNode = Graph->GetNodeWithID(Edge.ToNodeID);

				FInputHandle Input = InputNode->GetInputWithID(Edge.ToVertexID);
				if (Input->IsValid())
				{
					Inputs.Add(Input);
				}
			}

			return Inputs;
		}

		TArray<FConstInputHandle> FBaseOutputController::GetConstConnectedInputs() const 
		{
			TArray<FConstInputHandle> Inputs;

			// Create output handle from output node.
			FConstGraphHandle Graph = OwningNode->GetOwningGraph();

			for (const FMetasoundFrontendEdge& Edge : FindEdges())
			{
				FConstNodeHandle InputNode = Graph->GetNodeWithID(Edge.ToNodeID);

				FConstInputHandle Input = InputNode->GetInputWithID(Edge.ToVertexID);
				if (Input->IsValid())
				{
					Inputs.Add(Input);
				}
			}

			return Inputs;
		}

		bool FBaseOutputController::Disconnect() 
		{
			bool bSuccess = true;
			for (FInputHandle Input : GetConnectedInputs())
			{
				if (Input->IsValid())
				{
					bSuccess &= Disconnect(*Input);
				}
			}
			return bSuccess;
		}

		FConnectability FBaseOutputController::CanConnectTo(const IInputController& InController) const
		{
			return InController.CanConnectTo(*this);
		}

		bool FBaseOutputController::Connect(IInputController& InController)
		{
			return InController.Connect(*this);
		}

		bool FBaseOutputController::ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return InController.ConnectWithConverterNode(*this, InNodeClassName);
		}

		bool FBaseOutputController::Disconnect(IInputController& InController)
		{
			return InController.Disconnect(*this);
		}

		TArray<FMetasoundFrontendEdge> FBaseOutputController::FindEdges() const
		{
			if (GraphPtr.IsValid())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingSource = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.FromNodeID == NodeID) && (Edge.FromVertexID == VertexID);
				};

				return GraphPtr->Edges.FilterByPredicate(EdgeHasMatchingSource);
			}

			return TArray<FMetasoundFrontendEdge>();
		}

		const FText& FBaseOutputController::GetDisplayName() const
		{
			if (ClassOutputPtr.IsValid())
			{
				return ClassOutputPtr->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FBaseOutputController::GetTooltip() const
		{
			if (ClassOutputPtr.IsValid())
			{
				return ClassOutputPtr->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FBaseOutputController::GetMetadata() const
		{
			if (ClassOutputPtr.IsValid())
			{
				return ClassOutputPtr->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FBaseOutputController::ShareAccess()
		{
			FDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassOutput = ClassOutputPtr;
			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FBaseOutputController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassOutput = ClassOutputPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		//
		// FInputNodeOutputController
		// 
		FInputNodeOutputController::FInputNodeOutputController(const FInputNodeOutputController::FInitParams& InParams)
		: FBaseOutputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassOutputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		{
		}

		bool FInputNodeOutputController::IsValid() const
		{
			return FBaseOutputController::IsValid() && OwningGraphClassInputPtr.IsValid();
		}

		const FText& FInputNodeOutputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = ClassOutputPtr.Get())
				{
					// If there is a valid ClassOutput, combine the names.
					CachedDisplayName = FText::Format(LOCTEXT("InputNodeOutputControllerFormat", "{1} {0}"), OwningInput->Metadata.DisplayName, ClassOutput->Metadata.DisplayName);
				}
				else
				{
					// If there is no valid ClassOutput, use the owning value display name.
					CachedDisplayName = OwningInput->Metadata.DisplayName;
				}
			}

			return CachedDisplayName;
		}

		const FText& FInputNodeOutputController::GetTooltip() const
		{
			if (OwningGraphClassInputPtr.IsValid())
			{
				return OwningGraphClassInputPtr->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FInputNodeOutputController::GetMetadata() const
		{
			if (OwningGraphClassInputPtr.IsValid())
			{
				return OwningGraphClassInputPtr->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FInputNodeOutputController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseOutputController::ShareAccess();

			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		FConstDocumentAccess FInputNodeOutputController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseOutputController::ShareAccess();

			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		//
		// FOutputNodeOutputController
		// 
		FOutputNodeOutputController::FOutputNodeOutputController(const FOutputNodeOutputController::FInitParams& InParams)
		: FBaseOutputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassOutputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		bool FOutputNodeOutputController::IsValid() const
		{
			return FBaseOutputController::IsValid() && OwningGraphClassOutputPtr.IsValid();
		}

		const FText& FOutputNodeOutputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FOutputNodeOutputController::GetTooltip() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FOutputNodeOutputController::GetMetadata() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FConnectability FOutputNodeOutputController::CanConnectTo(const IInputController& InController) const 
		{
			// Cannot connect to a graph's output.
			static const FConnectability Connectability = {FConnectability::EConnectable::No};

			return Connectability;
		}

		bool FOutputNodeOutputController::Connect(IInputController& InController) 
		{
			return false;
		}

		bool FOutputNodeOutputController::ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return false;
		}

		//
		// FBaseInputController
		// 
		FBaseInputController::FBaseInputController(const FBaseInputController::FInitParams& InParams)
		: ID(InParams.ID)
		, NodeVertexPtr(InParams.NodeVertexPtr)
		, ClassInputPtr(InParams.ClassInputPtr)
		, GraphPtr(InParams.GraphPtr)
		, OwningNode(InParams.OwningNode)
		{
		}

		bool FBaseInputController::IsValid() const 
		{
			return OwningNode->IsValid() && NodeVertexPtr.IsValid() &&  GraphPtr.IsValid();
		}

		FGuid FBaseInputController::GetID() const
		{
			return ID;
		}

		const FName& FBaseInputController::GetDataType() const
		{
			if (NodeVertexPtr.IsValid())
			{
				return NodeVertexPtr->TypeName;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FName>();
		}

		const FString& FBaseInputController::GetName() const
		{
			if (NodeVertexPtr.IsValid())
			{
				return NodeVertexPtr->Name;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		const FText& FBaseInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata.DisplayName;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendLiteral* FBaseInputController::GetLiteral() const
		{
			if (NodeVertexPtr.IsValid() && OwningNode->IsValid())
			{
				const FGuid& VertexID = NodeVertexPtr->VertexID;
				return OwningNode->GetInputLiteral(VertexID);
			}

			return nullptr;
		}

		void FBaseInputController::SetLiteral(const FMetasoundFrontendLiteral& InLiteral)
		{
			if (NodeVertexPtr.IsValid() && OwningNode->IsValid())
			{
				const FGuid& VertexID = NodeVertexPtr->VertexID;
				if (const FMetasoundFrontendLiteral* ClassLiteral = GetClassDefaultLiteral())
				{
					// Clear if equivalent to class default as fallback is the class default
					if (ClassLiteral->IsEquivalent(InLiteral))
					{
						OwningNode->ClearInputLiteral(VertexID);
						return;
					}
				}

				OwningNode->SetInputLiteral(FMetasoundFrontendVertexLiteral{ VertexID, InLiteral });
			}
		}

		const FMetasoundFrontendLiteral* FBaseInputController::GetClassDefaultLiteral() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return &(ClassInput->DefaultLiteral);
			}
			return nullptr;
		}

		const FText& FBaseInputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FBaseInputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		bool FBaseInputController::IsConnected() const 
		{
			return (nullptr != FindEdge());
		}

		FGuid FBaseInputController::GetOwningNodeID() const
		{
			return OwningNode->GetID();
		}

		FNodeHandle FBaseInputController::GetOwningNode()
		{
			return OwningNode;
		}

		FConstNodeHandle FBaseInputController::GetOwningNode() const
		{
			return OwningNode;
		}

		FOutputHandle FBaseInputController::GetConnectedOutput()
		{
			if (const FMetasoundFrontendEdge* Edge = FindEdge())
			{
				// Create output handle from output node.
				FGraphHandle Graph = OwningNode->GetOwningGraph();
				FNodeHandle OutputNode = Graph->GetNodeWithID(Edge->FromNodeID);
				return OutputNode->GetOutputWithID(Edge->FromVertexID);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConstOutputHandle FBaseInputController::GetConnectedOutput() const
		{
			if (const FMetasoundFrontendEdge* Edge = FindEdge())
			{
				// Create output handle from output node.
				FConstGraphHandle Graph = OwningNode->GetOwningGraph();
				FConstNodeHandle OutputNode = Graph->GetNodeWithID(Edge->FromNodeID);
				return OutputNode->GetOutputWithID(Edge->FromVertexID);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConnectability FBaseInputController::CanConnectTo(const IOutputController& InController) const
		{
			FConnectability OutConnectability;
			OutConnectability.Connectable = FConnectability::EConnectable::No;

			if (!(InController.IsValid() && IsValid()))
			{
				return OutConnectability;
			}

			if (InController.GetDataType() == GetDataType())
			{
				// If data types are equal, connection can happen.
				OutConnectability.Connectable = FConnectability::EConnectable::Yes;
				return OutConnectability;
			}

			// If data types are not equal, check for converter nodes which could
			// convert data type.
			OutConnectability.PossibleConverterNodeClasses = FRegistry::Get()->GetPossibleConverterNodes(InController.GetDataType(), GetDataType());

			if (OutConnectability.PossibleConverterNodeClasses.Num() > 0)
			{
				OutConnectability.Connectable = FConnectability::EConnectable::YesWithConverterNode;
				return OutConnectability;
			}

			return OutConnectability;
		}

		bool FBaseInputController::Connect(IOutputController& InController)
		{
			if (!IsValid() || !InController.IsValid())
			{
				return false;
			}

			if (ensureAlwaysMsgf(InController.GetDataType() == GetDataType(), TEXT("Cannot connect incompatible types.")))
			{
				// Overwrite an existing connection if it exists.
				FMetasoundFrontendEdge* Edge = FindEdge();

				if (!Edge)
				{
					Edge = &GraphPtr->Edges.AddDefaulted_GetRef();
					Edge->ToNodeID = GetOwningNodeID();
					Edge->ToVertexID = GetID();
				}

				Edge->FromNodeID = InController.GetOwningNodeID();
				Edge->FromVertexID = InController.GetID();

				return true;
			}

			return false;
		}

		bool FBaseInputController::ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InConverterInfo)
		{
			FGraphHandle Graph = OwningNode->GetOwningGraph();

			// Generate the converter node.
			FNodeHandle ConverterNode = Graph->AddNode(InConverterInfo.NodeKey);

			TArray<FInputHandle> ConverterInputs = ConverterNode->GetInputsWithVertexName(InConverterInfo.PreferredConverterInputPin);
			TArray<FOutputHandle> ConverterOutputs = ConverterNode->GetOutputsWithVertexName(InConverterInfo.PreferredConverterOutputPin);

			if (ConverterInputs.Num() < 1)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Converter node [Name: %s] does not support preferred input vertex [Vertex: %s]"), *ConverterNode->GetNodeName(), *InConverterInfo.PreferredConverterInputPin);
				return false;
			}

			if (ConverterOutputs.Num() < 1)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Converter node [Name: %s] does not support preferred output vertex [Vertex: %s]"), *ConverterNode->GetNodeName(), *InConverterInfo.PreferredConverterOutputPin);
				return false;
			}

			FInputHandle ConverterInput = ConverterInputs[0];
			FOutputHandle ConverterOutput = ConverterOutputs[0];

			// Connect the output InController to the converter, than connect the converter to this input.
			if (ConverterInput->Connect(InController) && Connect(*ConverterOutput))
			{
				return true;
			}

			return false;
		}

		bool FBaseInputController::Disconnect(IOutputController& InController) 
		{
			if (GraphPtr.IsValid())
			{
				FGuid FromNodeID = InController.GetOwningNodeID();
				FGuid FromVertexID = InController.GetID();
				FGuid ToNodeID = GetOwningNodeID();
				FGuid ToVertexID = GetID();

				auto IsMatchingEdge = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.FromNodeID == FromNodeID) && (Edge.FromVertexID == FromVertexID) && (Edge.ToNodeID == ToNodeID) && (Edge.ToVertexID == ToVertexID);
				};

				int32 NumRemoved = GraphPtr->Edges.RemoveAllSwap(IsMatchingEdge);
				return NumRemoved > 0;
			}

			return false;
		}

		bool FBaseInputController::Disconnect()
		{
			if (GraphPtr.IsValid())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				int32 NumRemoved = GraphPtr->Edges.RemoveAllSwap(EdgeHasMatchingDestination);
				return NumRemoved > 0;
			}

			return false;
		}

		const FMetasoundFrontendEdge* FBaseInputController::FindEdge() const
		{
			if (GraphPtr.IsValid())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				return GraphPtr->Edges.FindByPredicate(EdgeHasMatchingDestination);
			}

			return nullptr;
		}

		FMetasoundFrontendEdge* FBaseInputController::FindEdge()
		{
			if (GraphPtr.IsValid())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				return GraphPtr->Edges.FindByPredicate(EdgeHasMatchingDestination);
			}

			return nullptr;
		}

		FDocumentAccess FBaseInputController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassInput = ClassInputPtr;
			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FBaseInputController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassInput = ClassInputPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}


		//
		// FOutputNodeInputController
		//
		FOutputNodeInputController::FOutputNodeInputController(const FOutputNodeInputController::FInitParams& InParams)
		: FBaseInputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassInputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		bool FOutputNodeInputController::IsValid() const
		{
			return FBaseInputController::IsValid() && OwningGraphClassOutputPtr.IsValid();
		}

		const FText& FOutputNodeInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
				{
					// If there the ClassInput exists, combine the variable name and class input name.
					// of the variable should be added to the names of the vertices.
					CachedDisplayName = FText::Format(LOCTEXT("OutputNodeInputControllerFormat", "{1} {0}"), OwningOutput->Metadata.DisplayName, ClassInput->Metadata.DisplayName);
				}
				else
				{
					// If there is not ClassInput, then use the variable name.
					CachedDisplayName = OwningOutput->Metadata.DisplayName;
				}
			}

			return CachedDisplayName;
		}

		const FText& FOutputNodeInputController::GetTooltip() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FOutputNodeInputController::GetMetadata() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FOutputNodeInputController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseInputController::ShareAccess();

			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}

		FConstDocumentAccess FOutputNodeInputController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseInputController::ShareAccess();

			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}


		//
		// FInputNodeInputController
		//
		FInputNodeInputController::FInputNodeInputController(const FInputNodeInputController::FInitParams& InParams)
		: FBaseInputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassInputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		{
		}

		bool FInputNodeInputController::IsValid() const
		{
			return FBaseInputController::IsValid() && OwningGraphClassInputPtr.IsValid();
		}

		const FText& FInputNodeInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeInputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FInputNodeInputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FConnectability FInputNodeInputController::CanConnectTo(const IOutputController& InController) const 
		{
			static const FConnectability Connectability = {FConnectability::EConnectable::No};
			return Connectability;
		}

		bool FInputNodeInputController::Connect(IOutputController& InController) 
		{
			return false;
		}

		bool FInputNodeInputController::ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return false;
		}

		//
		// FBaseNodeController
		//
		FBaseNodeController::FBaseNodeController(const FBaseNodeController::FInitParams& InParams)
		: NodePtr(InParams.NodePtr)
		, ClassPtr(InParams.ClassPtr)
		, OwningGraph(InParams.OwningGraph)
		{
			if (NodePtr.IsValid() && ClassPtr.IsValid())
			{
				if (NodePtr->ClassID != ClassPtr->ID)
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Changing node's class id from [ClassID:%s] to [ClassID:%s]"), *NodePtr->ClassID.ToString(), *ClassPtr->ID.ToString());
					NodePtr->ClassID = ClassPtr->ID;
				}
			}
		}

		bool FBaseNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && NodePtr.IsValid() && ClassPtr.IsValid();
		}

		FGuid FBaseNodeController::GetOwningGraphClassID() const
		{
			return OwningGraph->GetClassID();
		}

		FGraphHandle FBaseNodeController::GetOwningGraph()
		{
			return OwningGraph;
		}

		FConstGraphHandle FBaseNodeController::GetOwningGraph() const
		{
			return OwningGraph;
		}

		FGuid FBaseNodeController::GetID() const
		{
			if (NodePtr.IsValid())
			{
				return NodePtr->ID;
			}
			return Metasound::FrontendInvalidID;
		}

		FGuid FBaseNodeController::GetClassID() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->ID;
			}
			return Metasound::FrontendInvalidID;
		}

		const FMetasoundFrontendLiteral* FBaseNodeController::GetInputLiteral(const FGuid& InVertexID) const
		{
			if (NodePtr.IsValid())
			{
				for (const FMetasoundFrontendVertexLiteral& VertexLiteral : NodePtr->InputLiterals)
				{
					if (VertexLiteral.VertexID == InVertexID)
					{
						return &VertexLiteral.Value;
					}
				}
			}

			return nullptr;
		}

		void FBaseNodeController::SetInputLiteral(const FMetasoundFrontendVertexLiteral& InVertexLiteral)
		{
			if (NodePtr.IsValid())
			{
				auto IsInputVertex = [InVertexLiteral] (const FMetasoundFrontendVertex& Vertex)
				{
					return InVertexLiteral.VertexID == Vertex.VertexID;
				};

				FMetasoundFrontendNodeInterface& NodeInterface = NodePtr->Interface;
				if (!ensure(NodeInterface.Inputs.ContainsByPredicate(IsInputVertex)))
				{
					return;
				}

				for (FMetasoundFrontendVertexLiteral& VertexLiteral : NodePtr->InputLiterals)
				{
					if (VertexLiteral.VertexID == InVertexLiteral.VertexID)
					{
						if (ensure(VertexLiteral.Value.GetType() == InVertexLiteral.Value.GetType()))
						{
							VertexLiteral = InVertexLiteral;
						}
						return;
					}
				}

				NodePtr->InputLiterals.Add(InVertexLiteral);
			}
		}

		bool FBaseNodeController::ClearInputLiteral(FGuid InVertexID)
		{
			if (NodePtr.IsValid())
			{
				auto IsInputVertex = [InVertexID](const FMetasoundFrontendVertexLiteral& VertexLiteral)
				{
					return InVertexID == VertexLiteral.VertexID;
				};

				return NodePtr->InputLiterals.RemoveAllSwap(IsInputVertex, false) > 0;
			}

			return false;
		}

		const FMetasoundFrontendClassInterface& FBaseNodeController::GetClassInterface() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Interface;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassInterface>();
		}

		const FMetasoundFrontendClassMetadata& FBaseNodeController::GetClassMetadata() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Metadata;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassMetadata>();
		}

		const FMetasoundFrontendInterfaceStyle& FBaseNodeController::GetInputStyle() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Interface.InputStyle;
			}

			static const FMetasoundFrontendInterfaceStyle Invalid;
			return Invalid;
		}

		const FMetasoundFrontendInterfaceStyle& FBaseNodeController::GetOutputStyle() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Interface.OutputStyle;
			}

			static const FMetasoundFrontendInterfaceStyle Invalid;
			return Invalid;
		}

		const FMetasoundFrontendClassStyle& FBaseNodeController::GetClassStyle() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Style;
			}

			static const FMetasoundFrontendClassStyle Invalid;
			return Invalid;
		}

		const FMetasoundFrontendNodeStyle& FBaseNodeController::GetNodeStyle() const
		{
			if (NodePtr.IsValid())
			{
				return NodePtr->Style;
			}

			static const FMetasoundFrontendNodeStyle Invalid;
			return Invalid;
		}

		void FBaseNodeController::SetNodeStyle(const FMetasoundFrontendNodeStyle& InStyle)
		{
			if (NodePtr.IsValid())
			{
				NodePtr->Style = InStyle;
			}
		}

		const FText& FBaseNodeController::GetDescription() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Metadata.Description;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FString& FBaseNodeController::GetNodeName() const
		{
			if (NodePtr.IsValid())
			{
				return NodePtr->Name;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		bool FBaseNodeController::CanAddInput(const FString& InVertexName) const
		{
			// TODO: not yet supported
			return false;
		}

		FInputHandle FBaseNodeController::AddInput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault)
		{
			checkNoEntry();
			// TODO: not yet supported
			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::RemoveInput(FGuid InVertexID)
		{
			checkNoEntry();
			// TODO: not yet supported
			return false;
		}

		bool FBaseNodeController::CanAddOutput(const FString& InVertexName) const
		{
			// TODO: not yet supported
			return false;
		}

		FInputHandle FBaseNodeController::AddOutput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault)
		{
			checkNoEntry();
			// TODO: not yet supported
			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::RemoveOutput(FGuid InVertexID)
		{
			checkNoEntry();
			// TODO: not yet supported
			return false;
		}

		TArray<FInputHandle> FBaseNodeController::GetInputs()
		{
			TArray<FInputHandle> Inputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		int32 FBaseNodeController::GetNumInputs() const
		{
			if (NodePtr.IsValid())
			{
				return NodePtr->Interface.Inputs.Num();
			}

			return 0;
		}

		void FBaseNodeController::IterateInputs(TUniqueFunction<void(FInputHandle)> InFunction)
		{
			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, AsShared());
				if (InputHandle->IsValid())
				{
					InFunction(InputHandle);
				}
			}
		}

		TArray<FOutputHandle> FBaseNodeController::GetOutputs()
		{
			TArray<FOutputHandle> Outputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		int32 FBaseNodeController::GetNumOutputs() const
		{
			if (NodePtr.IsValid())
			{
				return NodePtr->Interface.Outputs.Num();
			}

			return 0;
		}

		TArray<FConstInputHandle> FBaseNodeController::GetConstInputs() const
		{
			TArray<FConstInputHandle> Inputs;

			// If I had a nickle for every time C++ backed me into a corner, I would be sitting
			// on a tropical beach next to my mansion sipping strawberry daiquiris instead of 
			// trying to code using this guileful language. The const cast is generally safe here
			// because the FConstInputHandle only allows const access to the internal node controller. 
			// Ostensibly, there could have been a INodeController and IConstNodeController
			// which take different types in their constructor, but it starts to become
			// difficult to maintain. So instead of adding 500 lines of nearly duplicate 
			// code, a ConstCastSharedRef is used here. 
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FConstInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		void FBaseNodeController::IterateOutputs(TUniqueFunction<void(FOutputHandle)> InFunction)
		{
			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, AsShared());
				if (OutputHandle->IsValid())
				{
					InFunction(OutputHandle);
				}
			}
		}

		const FText& FBaseNodeController::GetDisplayTitle() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FBaseNodeController::GetDisplayName() const
		{
			if (ClassPtr.IsValid())
			{
				return ClassPtr->Metadata.DisplayName;
			}
			return FText::GetEmpty();
		}

		void FBaseNodeController::IterateConstInputs(TUniqueFunction<void(FConstInputHandle)> InFunction) const
		{
			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FConstInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					InFunction(InputHandle);
				}
			}
		}

		TArray<FConstOutputHandle> FBaseNodeController::GetConstOutputs() const
		{
			TArray<FConstOutputHandle> Outputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		void FBaseNodeController::IterateConstOutputs(TUniqueFunction<void(FConstOutputHandle)> InFunction) const
		{
			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					InFunction(OutputHandle);
				}
			}
		}

		TArray<FInputHandle> FBaseNodeController::GetInputsWithVertexName(const FString& InName) 
		{
			TArray<FInputHandle> Inputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FInputControllerParams& Params : GetInputControllerParamsWithVertexName(InName))
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		TArray<FConstInputHandle> FBaseNodeController::GetConstInputsWithVertexName(const FString& InName) const 
		{
			TArray<FConstInputHandle> Inputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParamsWithVertexName(InName))
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		TArray<FOutputHandle> FBaseNodeController::GetOutputsWithVertexName(const FString& InName)
		{
			TArray<FOutputHandle> Outputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FOutputControllerParams& Params : GetOutputControllerParamsWithVertexName(InName))
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		TArray<FConstOutputHandle> FBaseNodeController::GetConstOutputsWithVertexName(const FString& InName) const
		{
			TArray<FConstOutputHandle> Outputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParamsWithVertexName(InName))
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		FInputHandle FBaseNodeController::GetInputWithID(FGuid InVertexID)
		{
			FInputControllerParams Params;

			if (FindInputControllerParamsWithID(InVertexID, Params))
			{
				FNodeHandle ThisNode = this->AsShared();
				return CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
			}

			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::IsRequired(const FMetasoundFrontendArchetype& InArchetype) const
		{
			return false;
		}

		FConstInputHandle FBaseNodeController::GetInputWithID(FGuid InVertexID) const
		{
			FInputControllerParams Params;

			if (FindInputControllerParamsWithID(InVertexID, Params))
			{
				// See early use of ConstCastSharedRef in this class for discussion.
				FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());
				return CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
			}

			return FInvalidInputController::GetInvalid();
		}

		FOutputHandle FBaseNodeController::GetOutputWithID(FGuid InVertexID)
		{
			FOutputControllerParams Params;

			if (FindOutputControllerParamsWithID(InVertexID, Params))
			{
				FNodeHandle ThisNode = this->AsShared();
				return CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConstOutputHandle FBaseNodeController::GetOutputWithID(FGuid InVertexID) const
		{
			FOutputControllerParams Params;

			if (FindOutputControllerParamsWithID(InVertexID, Params))
			{
				// See early use of ConstCastSharedRef in this class for discussion.
				FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());
				return CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
			}

			return FInvalidOutputController::GetInvalid();
		}

		TArray<FBaseNodeController::FInputControllerParams> FBaseNodeController::GetInputControllerParams() const
		{
			TArray<FBaseNodeController::FInputControllerParams> Inputs;

			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				for (const FMetasoundFrontendVertex& NodeInputVertex : Node->Interface.Inputs)
				{
					FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithName(NodeInputVertex.Name);
					FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(NodeInputVertex.Name);

					Inputs.Add({NodeInputVertex.VertexID, NodeVertexPtr, ClassInputPtr});
				}
			}

			return Inputs;
		}

		TArray<FBaseNodeController::FOutputControllerParams> FBaseNodeController::GetOutputControllerParams() const
		{
			TArray<FBaseNodeController::FOutputControllerParams> Outputs;

			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				for (const FMetasoundFrontendVertex& NodeOutputVertex : Node->Interface.Outputs)
				{
					const FString& VertexName = NodeOutputVertex.Name;

					FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithName(VertexName);
					FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(VertexName);

					Outputs.Add({NodeOutputVertex.VertexID, NodeVertexPtr, ClassOutputPtr});
				}
			}

			return Outputs;
		}

		TArray<FBaseNodeController::FInputControllerParams> FBaseNodeController::GetInputControllerParamsWithVertexName(const FString& InName) const
		{
			TArray<FBaseNodeController::FInputControllerParams> Inputs;

			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithName(InName);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(InName);

				Inputs.Add({Vertex->VertexID, NodeVertexPtr, ClassInputPtr});
			}

			return Inputs;
		}

		TArray<FBaseNodeController::FOutputControllerParams> FBaseNodeController::GetOutputControllerParamsWithVertexName(const FString& InName) const
		{
			TArray<FBaseNodeController::FOutputControllerParams> Outputs;

			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithName(InName);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(InName);

				Outputs.Add({Vertex->VertexID, NodeVertexPtr, ClassOutputPtr});
			}

			return Outputs;
		}

		bool FBaseNodeController::FindInputControllerParamsWithID(FGuid InVertexID, FInputControllerParams& OutParams) const
		{
			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithVertexID(InVertexID);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(Vertex->Name);

				OutParams = FInputControllerParams{InVertexID, NodeVertexPtr, ClassInputPtr};
				return true;
			}

			return false;
		}

		bool FBaseNodeController::FindOutputControllerParamsWithID(FGuid InVertexID, FOutputControllerParams& OutParams) const
		{
			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithVertexID(InVertexID);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(Vertex->Name);

				OutParams = FOutputControllerParams{InVertexID, NodeVertexPtr, ClassOutputPtr};
				return true;
			}

			return false;
		}

		FGraphHandle FBaseNodeController::AsGraph()
		{
			// TODO: consider adding support for external graph owned in another document.
			// Will require lookup support for external subgraphs..
			
			if (ClassPtr.IsValid())
			{
				return GetOwningGraph()->GetOwningDocument()->GetSubgraphWithClassID(ClassPtr->ID);
			}

			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FBaseNodeController::AsGraph() const
		{
			// TODO: add support for graph owned in another asset.
			// Will require lookup support for external subgraphs..
			if (ClassPtr.IsValid())
			{
				return GetOwningGraph()->GetOwningDocument()->GetSubgraphWithClassID(ClassPtr->ID);
			}

			return FInvalidGraphController::GetInvalid();
		}


		FDocumentAccess FBaseNodeController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.Node = NodePtr;
			Access.ConstNode = NodePtr;
			Access.ConstClass = ClassPtr;

			return Access;
		}

		FConstDocumentAccess FBaseNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstNode = NodePtr;
			Access.ConstClass = ClassPtr;

			return Access;
		}


		// 
		// FNodeController
		//
		FNodeController::FNodeController(EPrivateToken InToken, const FNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, GraphPtr(InParams.GraphPtr)
		{
		}

		FNodeHandle FNodeController::CreateNodeHandle(const FNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				// Cannot make a valid node handle if the node description and class description differ
				if (InParams.NodePtr->ClassID == InParams.ClassPtr->ID)
				{
					return MakeShared<FNodeController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FNodeController::CreateConstNodeHandle(const FNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				// Cannot make a valid node handle if the node description and class description differ
				if (InParams.NodePtr->ClassID == InParams.ClassPtr->ID)
				{
					return MakeShared<const FNodeController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		bool FNodeController::IsValid() const
		{
			return FBaseNodeController::IsValid() && GraphPtr.IsValid();
		}

		FInputHandle FNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseInputController>(FBaseInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseOutputController>(FBaseOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, GraphPtr, InOwningNode});
		}

		FDocumentAccess FNodeController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;

			return Access;
		}


		//
		// FOutputNodeController
		//
		FOutputNodeController::FOutputNodeController(FOutputNodeController::EPrivateToken InToken, const FOutputNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, GraphPtr(InParams.GraphPtr)
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		FNodeHandle FOutputNodeController::CreateOutputNodeHandle(const FOutputNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				if (EMetasoundFrontendClassType::Output == InParams.ClassPtr->Metadata.Type)
				{
					if (InParams.ClassPtr->ID == InParams.NodePtr->ClassID)
					{
						return MakeShared<FOutputNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating output node.. Must be EMetasoundFrontendClassType::Output."), *InParams.ClassPtr->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		const FText& FOutputNodeController::GetDescription() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FOutputNodeController::GetDisplayName() const
		{
			if (OwningGraphClassOutputPtr.IsValid())
			{
				return OwningGraphClassOutputPtr->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		void FOutputNodeController::SetDescription(const FText& InDescription)
		{
			// TODO: can we remove the const cast by constructing output nodes with a non-const access to class outputs?
			if (FMetasoundFrontendClassOutput* ClassOutput = ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr).Get())
			{
				ClassOutput->Metadata.Description = InDescription;
			}
		}

		void FOutputNodeController::SetDisplayName(const FText& InDisplayName)
		{
			// TODO: can we remove the const cast by constructing output nodes with a non-const access to class outputs?
			if (FMetasoundFrontendClassOutput* ClassOutput = ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr).Get())
			{
				ClassOutput->Metadata.DisplayName = InDisplayName;
			}
		}

		FConstNodeHandle FOutputNodeController::CreateConstOutputNodeHandle(const FOutputNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				if (EMetasoundFrontendClassType::Output == InParams.ClassPtr->Metadata.Type)
				{
					if (InParams.ClassPtr->ID == InParams.NodePtr->ClassID)
					{
						return MakeShared<const FOutputNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating output node. Must be EMetasoundFrontendClassType::Output."), *InParams.ClassPtr->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		const FText& FOutputNodeController::GetDisplayTitle() const
		{
			static FText OutputDisplayTitle = LOCTEXT("OutputNode_Title", "Output");
			return OutputDisplayTitle;
		}

		bool FOutputNodeController::IsRequired(const FMetasoundFrontendArchetype& InArchetype) const
		{
			if (NodePtr.IsValid() && OwningGraph->IsValid())
			{
				const FString& Name = NodePtr->Name;
				const TArray<FMetasoundFrontendClassVertex>& RequiredOutputs = InArchetype.Interface.Outputs;
				for (const FMetasoundFrontendClassVertex& OutputVertex : RequiredOutputs)
				{
					if (OutputVertex.Name == Name)
					{
						return true;
					}
				}
			}

			return false;
		}

		bool FOutputNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && OwningGraphClassOutputPtr.IsValid() && GraphPtr.IsValid();
		}

		FInputHandle FOutputNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FOutputNodeInputController>(FOutputNodeInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, OwningGraphClassOutputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FOutputNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FOutputNodeOutputController>(FOutputNodeOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, OwningGraphClassOutputPtr, GraphPtr, InOwningNode});
		}

		FDocumentAccess FOutputNodeController::ShareAccess()
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;
			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}

		FConstDocumentAccess FOutputNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;
			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}


		//
		// FInputNodeController
		//
		FInputNodeController::FInputNodeController(EPrivateToken InToken, const FInputNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		, GraphPtr(InParams.GraphPtr)
		{
		}

		FNodeHandle FInputNodeController::CreateInputNodeHandle(const FInputNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				if (EMetasoundFrontendClassType::Input == InParams.ClassPtr->Metadata.Type)
				{
					if (InParams.ClassPtr->ID == InParams.NodePtr->ClassID)
					{
						return MakeShared<FInputNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating input node. Must be EMetasoundFrontendClassType::Input."), *InParams.ClassPtr->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FInputNodeController::CreateConstInputNodeHandle(const FInputNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				if (EMetasoundFrontendClassType::Input == InParams.ClassPtr->Metadata.Type)
				{
					if (InParams.ClassPtr->ID == InParams.NodePtr->ClassID)
					{
						return MakeShared<const FInputNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating input node. Must be EMetasoundFrontendClassType::Input."), *InParams.ClassPtr->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FInputNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && OwningGraphClassInputPtr.IsValid() && GraphPtr.IsValid();
		}

		FInputHandle FInputNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FInputNodeInputController>(FInputNodeInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, OwningGraphClassInputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FInputNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FInputNodeOutputController>(FInputNodeOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, OwningGraphClassInputPtr, GraphPtr, InOwningNode});
		}

		const FText& FInputNodeController::GetDescription() const
		{
			if (OwningGraphClassInputPtr.IsValid())
			{
				return OwningGraphClassInputPtr->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeController::GetDisplayName() const
		{
			if (OwningGraphClassInputPtr.IsValid())
			{
				return OwningGraphClassInputPtr->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeController::GetDisplayTitle() const
		{
			static FText InputDisplayTitle = LOCTEXT("InputNode_Title", "Input");
			return InputDisplayTitle;
		}

		bool FInputNodeController::IsRequired(const FMetasoundFrontendArchetype& InArchetype) const
		{
			if (NodePtr.IsValid() && OwningGraph->IsValid())
			{
				const FString& Name = NodePtr->Name;
				const TArray<FMetasoundFrontendClassVertex>& RequiredInputs = InArchetype.Interface.Inputs;
				for (const FMetasoundFrontendClassVertex& InputVertex : RequiredInputs)
				{
					if (InputVertex.Name == Name)
					{
						return true;
					}
				}
			}

			return false;
		}

		void FInputNodeController::SetDescription(const FText& InDescription)
		{
			// TODO: can we remove these const casts by constructing FINputNodeController with non-const access to the class input?
			if (FMetasoundFrontendClassInput* ClassInput = ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr).Get())
			{
				ClassInput->Metadata.Description = InDescription;
			}
		}

		void FInputNodeController::SetDisplayName(const FText& InDisplayName)
		{
			// TODO: can we remove these const casts by constructing FINputNodeController with non-const access to the class input?
			if (FMetasoundFrontendClassInput* ClassInput = ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr).Get())
			{
				ClassInput->Metadata.DisplayName = InDisplayName;
			}
		}

		FDocumentAccess FInputNodeController::ShareAccess()
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;
			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		FConstDocumentAccess FInputNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;
			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		// 
		// FGraphController
		//
		FGraphController::FGraphController(EPrivateToken InToken, const FGraphController::FInitParams& InParams)
		: GraphClassPtr(InParams.GraphClassPtr)
		, OwningDocument(InParams.OwningDocument)
		{
		}

		FGraphHandle FGraphController::CreateGraphHandle(const FGraphController::FInitParams& InParams)
		{
			if (InParams.GraphClassPtr.IsValid())
			{
				if (InParams.GraphClassPtr->Metadata.Type == EMetasoundFrontendClassType::Graph)
				{
					return MakeShared<FGraphController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to make graph controller [ClassID:%s]. Class must be EMeatsoundFrontendClassType::Graph."), *InParams.GraphClassPtr->ID.ToString())
				}
			}
			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FGraphController::CreateConstGraphHandle(const FGraphController::FInitParams& InParams)
		{
			if (InParams.GraphClassPtr.IsValid())
			{
				if (InParams.GraphClassPtr->Metadata.Type == EMetasoundFrontendClassType::Graph)
				{
					return MakeShared<FGraphController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to make graph controller [ClassID:%s]. Class must be EMeatsoundFrontendClassType::Graph."), *InParams.GraphClassPtr->ID.ToString())
				}
			}
			return FInvalidGraphController::GetInvalid();
		}

		bool FGraphController::IsValid() const
		{
			return GraphClassPtr.IsValid() && OwningDocument->IsValid();
		}

		FGuid FGraphController::GetClassID() const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->ID;
			}

			return Metasound::FrontendInvalidID;
		}

		const FText& FGraphController::GetDisplayName() const 
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		TArray<FString> FGraphController::GetInputVertexNames() const 
		{
			TArray<FString> Names;

			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendClassInput& Input : GraphClassPtr->Interface.Inputs)
				{
					Names.Add(Input.Name);
				}
			}

			return Names;
		}

		TArray<FString> FGraphController::GetOutputVertexNames() const
		{
			TArray<FString> Names;

			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendClassOutput& Output : GraphClassPtr->Interface.Outputs)
				{
					Names.Add(Output.Name);
				}
			}

			return Names;
		}

		FConstClassInputAccessPtr FGraphController::FindClassInputWithName(const FString& InName) const
		{
			return GraphClassPtr.GetInputWithName(InName);
		}

		FConstClassOutputAccessPtr FGraphController::FindClassOutputWithName(const FString& InName) const
		{
			return GraphClassPtr.GetOutputWithName(InName);
		}

		FGuid FGraphController::GetVertexIDForInputVertex(const FString& InInputName) const
		{
			if (const FMetasoundFrontendClassInput* Input = FindClassInputWithName(InInputName).Get())
			{
				return Input->VertexID;
			}
			return Metasound::FrontendInvalidID;
		}

		FGuid FGraphController::GetVertexIDForOutputVertex(const FString& InOutputName) const
		{
			if (const FMetasoundFrontendClassOutput* Output = FindClassOutputWithName(InOutputName).Get())
			{
				return Output->VertexID;
			}
			return Metasound::FrontendInvalidID;
		}

		TArray<FNodeHandle> FGraphController::GetNodes()
		{
			return GetNodeHandles(GetNodesAndClasses());
		}

		TArray<FConstNodeHandle> FGraphController::GetConstNodes() const
		{
			return GetNodeHandles(GetNodesAndClasses());
		}

		FConstNodeHandle FGraphController::GetNodeWithID(FGuid InNodeID) const
		{
			auto IsNodeWithSameID = [&](const FMetasoundFrontendClass& NodeClas, const FMetasoundFrontendNode& Node) 
			{ 
				return Node.ID == InNodeID; 
			};
			return GetNodeByPredicate(IsNodeWithSameID);
		}

		FNodeHandle FGraphController::GetNodeWithID(FGuid InNodeID)
		{
			auto IsNodeWithSameID = [&](const FMetasoundFrontendClass& NodeClas, const FMetasoundFrontendNode& Node) 
			{ 
				return Node.ID == InNodeID; 
			};
			return GetNodeByPredicate(IsNodeWithSameID);
		}

		const FMetasoundFrontendGraphStyle& FGraphController::GetGraphStyle() const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Graph.Style;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendGraphStyle>();
		}

		void FGraphController::SetGraphStyle(const FMetasoundFrontendGraphStyle& InStyle)
		{
			if (GraphClassPtr.IsValid())
			{
				GraphClassPtr->Graph.Style = InStyle;
			}
		}

		TArray<FNodeHandle> FGraphController::GetOutputNodes()
		{
			auto IsOutputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.Type == EMetasoundFrontendClassType::Output; 
			};
			return GetNodesByPredicate(IsOutputNode);
		}

		TArray<FNodeHandle> FGraphController::GetInputNodes()
		{
			auto IsInputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.Type == EMetasoundFrontendClassType::Input; 
			};
			return GetNodesByPredicate(IsInputNode);
		}

		void FGraphController::IterateNodes(TUniqueFunction<void(FNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType)
		{
			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InClassType == EMetasoundFrontendClassType::Invalid || NodeClass->Metadata.Type == InClassType)
						{
							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							FNodeHandle NodeHandle = GetNodeHandle(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
							InFunction(NodeHandle);
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}
		}

		void FGraphController::IterateConstNodes(TUniqueFunction<void(FConstNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType) const
		{
			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InClassType == EMetasoundFrontendClassType::Invalid || NodeClass->Metadata.Type == InClassType)
						{
							FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							FConstNodeHandle NodeHandle = GetNodeHandle(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
							InFunction(NodeHandle);
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}
		}

		TArray<FConstNodeHandle> FGraphController::GetConstOutputNodes() const
		{
			auto IsOutputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.Type == EMetasoundFrontendClassType::Output; 
			};
			return GetNodesByPredicate(IsOutputNode);
		}

		TArray<FConstNodeHandle> FGraphController::GetConstInputNodes() const
		{
			auto IsInputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.Type == EMetasoundFrontendClassType::Input; 
			};
			return GetNodesByPredicate(IsInputNode);
		}

		bool FGraphController::ContainsOutputVertexWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsOutputVertexWithSameName = [&](const FMetasoundFrontendClassOutput& ClassOutput) 
				{ 
					return ClassOutput.Name == InName;
				};
				return GraphClass->Interface.Outputs.ContainsByPredicate(IsOutputVertexWithSameName);
			}
			return false;
		}

		bool FGraphController::ContainsInputVertexWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsInputVertexWithSameName = [&](const FMetasoundFrontendClassInput& ClassInput) 
				{ 
					return ClassInput.Name == InName;
				};
				return GraphClass->Interface.Inputs.ContainsByPredicate(IsInputVertexWithSameName);
			}
			return false;
		}

		FConstNodeHandle FGraphController::GetOutputNodeWithName(const FString& InName) const
		{
			auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.Type == EMetasoundFrontendClassType::Output) && (Node.Name == InName); 
			};
			return GetNodeByPredicate(IsOutputNodeWithSameName);
		}

		FConstNodeHandle FGraphController::GetInputNodeWithName(const FString& InName) const
		{
			auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.Type == EMetasoundFrontendClassType::Input) && (Node.Name == InName); 
			};
			return GetNodeByPredicate(IsInputNodeWithSameName);
		}

		FNodeHandle FGraphController::GetOutputNodeWithName(const FString& InName)
		{
			auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.Type == EMetasoundFrontendClassType::Output) && (Node.Name == InName); 
			};
			return GetNodeByPredicate(IsOutputNodeWithSameName);
		}

		FNodeHandle FGraphController::GetInputNodeWithName(const FString& InName)
		{
			auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.Type == EMetasoundFrontendClassType::Input) && (Node.Name == InName); 
			};
			return GetNodeByPredicate(IsInputNodeWithSameName);
		}

		FNodeHandle FGraphController::AddInputVertex(const FMetasoundFrontendClassInput& InClassInput)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsInputWithSameName = [&](const FMetasoundFrontendClassInput& ExistingDesc) { return ExistingDesc.Name == InClassInput.Name; };
				if (Algo::NoneOf(GraphClass->Interface.Inputs, IsInputWithSameName))
				{
					FNodeRegistryKey Key;
					if (FRegistry::GetInputNodeRegistryKeyForDataType(InClassInput.TypeName, Key))
					{
						if (FConstClassAccessPtr InputClass = OwningDocument->FindOrAddClass(Key))
						{
							FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*InputClass);

							Node.Name = InClassInput.Name;
							Node.ID = FGuid::NewGuid();

							auto IsVertexWithTypeName = [&](FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InClassInput.TypeName; };
							if (FMetasoundFrontendVertex* InputVertex = Node.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								InputVertex->Name = InClassInput.Name;
							}
							else
							{
								UE_LOG(LogMetaSound, Error, TEXT("Input node [TypeName:%s] does not contain input vertex with type [TypeName:%s]"), *InClassInput.TypeName.ToString(), *InClassInput.TypeName.ToString());
							}

							if (Node.Interface.Outputs.Num() == 1)
							{
								Node.Interface.Outputs[0].Name = InClassInput.Name;
							}
							else if (FMetasoundFrontendVertex* OutputVertex = Node.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								OutputVertex->Name = InClassInput.Name;
							}

							FMetasoundFrontendClassInput& NewInput = GraphClassPtr->Interface.Inputs.Add_GetRef(InClassInput);
							NewInput.NodeID = Node.ID;

							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							return GetNodeHandle(FGraphController::FNodeAndClass{NodePtr, InputClass});
						}
					}
					else 
					{
						UE_LOG(LogMetaSound, Display, TEXT("Failed to add input. No input node registered for data type [TypeName:%s]"), *InClassInput.TypeName.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Display, TEXT("Failed to add input. Input with same name \"%s\" exists in class [ClassID:%s]"), *InClassInput.Name, *GraphClassPtr->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		bool FGraphController::RemoveInputVertex(const FString& InName)
		{
			if (GraphClassPtr.IsValid())
			{
				auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& InClass, const FMetasoundFrontendNode& InNode) 
				{ 
					return (InClass.Metadata.Type == EMetasoundFrontendClassType::Input) && (InNode.Name == InName); 
				};

				for (const FNodeAndClass& NodeAndClass : GetNodesAndClassesByPredicate(IsInputNodeWithSameName))
				{
					return RemoveInput(*NodeAndClass.Node);
				}
			}

			return false;
		}

		FNodeHandle FGraphController::AddOutputVertex(const FMetasoundFrontendClassOutput& InClassOutput)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsOutputWithSameName = [&](const FMetasoundFrontendClassOutput& ExistingDesc) { return ExistingDesc.Name == InClassOutput.Name; };
				if (Algo::NoneOf(GraphClass->Interface.Outputs, IsOutputWithSameName))
				{
					FNodeRegistryKey Key;
					if (FRegistry::GetOutputNodeRegistryKeyForDataType(InClassOutput.TypeName, Key))
					{
						if (FConstClassAccessPtr OutputClass = OwningDocument->FindOrAddClass(Key))
						{
							FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*OutputClass);

							Node.Name = InClassOutput.Name;
							Node.ID = FGuid::NewGuid();

							// TODO: have something that checks if input node has valid interface.
							auto IsVertexWithTypeName = [&](FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InClassOutput.TypeName; };
							if (Node.Interface.Inputs.Num() == 1)
							{
								Node.Interface.Inputs[0].Name = InClassOutput.Name;
							}
							else if (FMetasoundFrontendVertex* InputVertex = Node.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								InputVertex->Name = InClassOutput.Name;
							}

							if (FMetasoundFrontendVertex* OutputVertex = Node.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								OutputVertex->Name = InClassOutput.Name;
							}
							else
							{
								UE_LOG(LogMetaSound, Error, TEXT("Output node [TypeName:%s] does not contain output vertex with type [TypeName:%s]"), *InClassOutput.TypeName.ToString(), *InClassOutput.TypeName.ToString());
							}

							FMetasoundFrontendClassOutput& NewOutput = GraphClassPtr->Interface.Outputs.Add_GetRef(InClassOutput);
							NewOutput.NodeID = Node.ID;

							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							return GetNodeHandle(FGraphController::FNodeAndClass { NodePtr, OutputClass });
						}
					}
					else 
					{
						UE_LOG(LogMetaSound, Display, TEXT("Failed to add output. No output node registered for data type [TypeName:%s]"), *InClassOutput.TypeName.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Display, TEXT("Failed to add output. Output with same name \"%s\" exists in class [ClassID:%s]"), *InClassOutput.Name, *GraphClassPtr->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FGraphController::RemoveOutputVertex(const FString& InName)
		{
			if (GraphClassPtr.IsValid())
			{
				auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& InClass, const FMetasoundFrontendNode& InNode) 
				{ 
					return (InClass.Metadata.Type == EMetasoundFrontendClassType::Output) && (InNode.Name == InName); 
				};

				for (const FNodeAndClass& NodeAndClass : GetNodesAndClassesByPredicate(IsOutputNodeWithSameName))
				{
					return RemoveOutput(*NodeAndClass.Node);
				}
			}

			return false;
		}

		// This can be used to determine what kind of property editor we should use for the data type of a given input.
		// Will return Invalid if the input couldn't be found, or if the input doesn't support any kind of literals.
		ELiteralType FGraphController::GetPreferredLiteralTypeForInputVertex(const FString& InInputName) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				return FRegistry::Get()->GetDesiredLiteralTypeForDataType(Desc->TypeName);
			}
			return ELiteralType::Invalid;
		}

		// For inputs whose preferred literal type is UObject or UObjectArray, this can be used to determine the UObject corresponding to that input's datatype.
		UClass* FGraphController::GetSupportedClassForInputVertex(const FString& InInputName)
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				return FRegistry::Get()->GetLiteralUClassForDataType(Desc->TypeName);
			}
			return nullptr;
		}

		FMetasoundFrontendLiteral FGraphController::GetDefaultInput(const FGuid& InVertexID) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				return Desc->DefaultLiteral;
			}

			return FMetasoundFrontendLiteral{};
		}

		bool FGraphController::SetDefaultInput(const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				if (ensure(DoesDataTypeSupportLiteralType(Desc->TypeName, InLiteral.GetType())))
				{
					Desc->DefaultLiteral = InLiteral;
					return true;
				}
				else
				{
					SetDefaultInputToDefaultLiteralOfType(InVertexID);
				}
			}

			return false;
		}

		bool FGraphController::SetDefaultInputToDefaultLiteralOfType(const FGuid& InVertexID)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				Metasound::FLiteral Literal = Frontend::GetDefaultParamForDataType(Desc->TypeName);
				Desc->DefaultLiteral.SetFromLiteral(Literal);

				return Desc->DefaultLiteral.IsValid();
			}

			return false;
		}

		const FText& FGraphController::GetInputDescription(const FString& InName) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				return Desc->Metadata.Description;
			}

			return FText::GetEmpty();
		}

		const FText& FGraphController::GetOutputDescription(const FString& InName) const
		{
			if (const FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				return Desc->Metadata.Description;
			}

			return FText::GetEmpty();
		}

		void FGraphController::SetInputDisplayName(const FString& InName, const FText& InDisplayName)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				Desc->Metadata.DisplayName = InDisplayName;
			}
		}

		void FGraphController::SetOutputDisplayName(const FString& InName, const FText& InDisplayName)
		{
			if (FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				Desc->Metadata.DisplayName = InDisplayName;
			}
		}

		void FGraphController::SetInputDescription(const FString& InName, const FText& InDescription)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				Desc->Metadata.Description = InDescription;
			}
		}

		void FGraphController::SetOutputDescription(const FString& InName, const FText& InDescription)
		{
			if (FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				Desc->Metadata.Description = InDescription;
			}
		}

		// This can be used to clear the current literal for a given input.
		// @returns false if the input name couldn't be found.
		bool FGraphController::ClearLiteralForInput(const FString& InInputName, FGuid InVertexID)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				Desc->DefaultLiteral.Clear();
			}

			return false;
		}

		FNodeHandle FGraphController::AddNode(const FNodeRegistryKey& InKey)
		{
			if (IsValid())
			{
				// Construct a FNodeClassInfo from this lookup key.
				if (FConstClassAccessPtr DependencyDescription = OwningDocument->FindOrAddClass(InKey))
				{
					return AddNode(DependencyDescription);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to find node class info with registry key [Key:%s]"), *InKey);
					return INodeController::GetInvalidHandle();
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FNodeHandle FGraphController::AddNode(const FMetasoundFrontendClassMetadata& InClassMetadata)
		{
			return AddNode(FRegistry::GetRegistryKey(InClassMetadata));
		}

		FNodeHandle FGraphController::AddDuplicateNode(const INodeController& InNode)
		{
			const FMetasoundFrontendClassMetadata& ClassMetadata = InNode.GetClassMetadata();

			if (EMetasoundFrontendClassType::Graph == ClassMetadata.Type)
			{
				// Add subgraph and dependencies if needed
				if (!OwningDocument->FindClass(ClassMetadata).IsValid())
				{
					// Class does not exist, need to add the subgraph
					OwningDocument->AddDuplicateSubgraph(*(InNode.AsGraph()));
				}
			}

			// TODO: will need to copy node interface when dynamic pins exist.
			if (FConstClassAccessPtr DependencyDescription = OwningDocument->FindOrAddClass(ClassMetadata))
			{
				return AddNode(DependencyDescription);
			}

			return INodeController::GetInvalidHandle();
		}

		// Remove the node corresponding to this node handle.
		// On success, invalidates the received node handle.
		bool FGraphController::RemoveNode(INodeController& InNode)
		{
			FGuid NodeID = InNode.GetID();
			auto IsNodeWithSameID = [&](const FMetasoundFrontendNode& InDesc) { return InDesc.ID == NodeID; };
			if (const FMetasoundFrontendNode* Desc = GraphClassPtr->Graph.Nodes.FindByPredicate(IsNodeWithSameID))
			{
				switch(InNode.GetClassMetadata().Type)
				{
					case EMetasoundFrontendClassType::Input:
					{
						return RemoveInput(*Desc);
					}

					case EMetasoundFrontendClassType::Output:
					{
						return RemoveOutput(*Desc);
					}

					case EMetasoundFrontendClassType::Variable:
					case EMetasoundFrontendClassType::External:
					case EMetasoundFrontendClassType::Graph:
					{
						return RemoveNode(*Desc);
					}

					default:
					case EMetasoundFrontendClassType::Invalid:
					{
						static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing switch case coverage for EMetasoundFrontendClassType.");
						checkNoEntry();
					}
				}
			}

			return false;
		}

		// Returns the metadata for the current graph, including the name, description and author.
		const FMetasoundFrontendClassMetadata& FGraphController::GetGraphMetadata() const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Metadata;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassMetadata>();
		}

		void FGraphController::SetGraphMetadata(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			if (GraphClassPtr.IsValid())
			{
				GraphClassPtr->Metadata = InMetadata;
			}
		}

		FNodeHandle FGraphController::CreateEmptySubgraph(const FMetasoundFrontendClassMetadata& InInfo)
		{
			if (IsValid())
			{
				if (InInfo.Type == EMetasoundFrontendClassType::Graph)
				{
					if (const FMetasoundFrontendClass* ExistingDependency = OwningDocument->FindClass(InInfo).Get())
					{
						UE_LOG(LogMetaSound, Error, TEXT("Cannot add new subgraph. Metasound class already exists with matching metadata Name: \"%s\", Version %d.%d"), *(ExistingDependency->Metadata.ClassName.GetFullName().ToString()), ExistingDependency->Metadata.Version.Major, ExistingDependency->Metadata.Version.Minor);
					}
					else if (FConstClassAccessPtr DependencyDescription = OwningDocument->FindOrAddClass(InInfo))
					{
						return AddNode(DependencyDescription);
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Incompatible Metasound NodeType encountered when attempting to create an empty subgraph.  NodeType must equal EMetasoundFrontendClassType::Graph"));
				}
			}
			
			return FInvalidNodeController::GetInvalid();
		}

		TUniquePtr<IOperator> FGraphController::BuildOperator(const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<IOperatorBuilder::FBuildErrorPtr>& OutBuildErrors) const
		{
			if (!IsValid())
			{
				return TUniquePtr<IOperator>(nullptr);
			}

			// TODO: Implement subgraph inflation step here.

			// TODO: bubble up errors. 
			TArray<FMetasoundFrontendGraphClass> Subgraphs = OwningDocument->GetSubgraphs();
			TArray<FMetasoundFrontendClass> Dependencies = OwningDocument->GetDependencies();

			TUniquePtr<FFrontendGraph> Graph = FFrontendGraphBuilder::CreateGraph(*GraphClassPtr, Subgraphs, Dependencies);

			if (!Graph.IsValid())
			{
				return TUniquePtr<IOperator>(nullptr);
			}

			FOperatorBuilder OperatorBuilder(FOperatorBuilderSettings::GetDefaultSettings());
			FBuildGraphParams BuildParams{*Graph, InSettings, FDataReferenceCollection{}, InEnvironment};
			return OperatorBuilder.BuildGraphOperator(BuildParams, OutBuildErrors);
		}

		FDocumentHandle FGraphController::GetOwningDocument()
		{
			return OwningDocument;
		}

		FConstDocumentHandle FGraphController::GetOwningDocument() const
		{
			return OwningDocument;
		}

		FNodeHandle FGraphController::AddNode(FConstClassAccessPtr InExistingDependency)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				if (const FMetasoundFrontendClass* NodeClass = InExistingDependency.Get())
				{
					FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*InExistingDependency);

					Node.ID = FGuid::NewGuid();

					FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					return GetNodeHandle(FGraphController::FNodeAndClass{NodePtr, InExistingDependency});
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FGraphController::RemoveNode(const FMetasoundFrontendNode& InDesc)
		{
			if (GraphClassPtr.IsValid())
			{
				auto IsEdgeForThisNode = [&](const FMetasoundFrontendEdge& ConDesc) { return (ConDesc.FromNodeID == InDesc.ID) || (ConDesc.ToNodeID == InDesc.ID); };

				// Remove any reference connections
				int32 NumRemoved = GraphClassPtr->Graph.Edges.RemoveAll(IsEdgeForThisNode);

				auto IsNodeWithID = [&](const FMetasoundFrontendNode& Desc) { return InDesc.ID == Desc.ID; };

				NumRemoved += GraphClassPtr->Graph.Nodes.RemoveAll(IsNodeWithID);

				OwningDocument->SynchronizeDependencies();

				return (NumRemoved > 0);
			}
			return false;
		}

		bool FGraphController::RemoveInput(const FMetasoundFrontendNode& InNode)
		{
			if (GraphClassPtr.IsValid())
			{
				auto IsInputWithSameNodeID = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.NodeID == InNode.ID; };

				int32 NumInputsRemoved = GraphClassPtr->Interface.Inputs.RemoveAll(IsInputWithSameNodeID);

				bool bDidRemoveNode = RemoveNode(InNode);

				return (NumInputsRemoved > 0) || bDidRemoveNode;
			}

			return false;
		}

		bool FGraphController::RemoveOutput(const FMetasoundFrontendNode& InNode)
		{
			if (GraphClassPtr.IsValid())
			{
				auto IsOutputWithSameNodeID = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.NodeID == InNode.ID; };

				int32 NumOutputsRemoved = GraphClassPtr->Interface.Outputs.RemoveAll(IsOutputWithSameNodeID);

				bool bDidRemoveNode = RemoveNode(InNode);

				return (NumOutputsRemoved > 0) || bDidRemoveNode;
			}

			return false;
		}

		bool FGraphController::ContainsNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			if (GraphClassPtr.IsValid())
			{
				for (FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					const FMetasoundFrontendClass* NodeClass = OwningDocument->FindClassWithID(Node.ClassID).Get();
					if (nullptr != NodeClass)
					{
						if (InPredicate(*NodeClass, Node))
						{
							return true;
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return false;
		}

		TArray<FGraphController::FNodeAndClass> FGraphController::GetNodesAndClasses()
		{
			TArray<FNodeAndClass> NodesAndClasses;

			if (GraphClassPtr.IsValid())
			{
				for (FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);

					if (NodeClassPtr && NodePtr)
					{
						NodesAndClasses.Add(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FConstNodeAndClass> FGraphController::GetNodesAndClasses() const
		{
			TArray<FConstNodeAndClass> NodesAndClasses;

			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);

					if (NodeClassPtr && NodePtr)
					{
						NodesAndClasses.Add(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FNodeAndClass> FGraphController::GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate)
		{

			TArray<FNodeAndClass> NodesAndClasses;

			if (GraphClassPtr.IsValid())
			{
				for (FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InPredicate(*NodeClass, Node))
						{
							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							NodesAndClasses.Add(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FConstNodeAndClass> FGraphController::GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			TArray<FConstNodeAndClass> NodesAndClasses;

			if (GraphClassPtr.IsValid())
			{
				for (const FMetasoundFrontendNode& Node : GraphClassPtr->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InPredicate(*NodeClass, Node))
						{
							FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							NodesAndClasses.Add(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		FNodeHandle FGraphController::GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate)
		{
			if (GraphClassPtr.IsValid())
			{
				TArray<FNodeAndClass> NodeAndClass = GetNodesAndClassesByPredicate(InPredicate);

				if (NodeAndClass.Num() > 0)
				{
					return GetNodeHandle(NodeAndClass[0]);
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FGraphController::GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			if (GraphClassPtr.IsValid())
			{
				TArray<FConstNodeAndClass> NodeAndClass = GetNodesAndClassesByPredicate(InPredicate);

				if (NodeAndClass.Num() > 0)
				{
					return GetNodeHandle(NodeAndClass[0]);
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		TArray<FNodeHandle> FGraphController::GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc)
		{
			if (GraphClassPtr.IsValid())
			{
				return GetNodeHandles(GetNodesAndClassesByPredicate(InFilterFunc));
			}
			return TArray<FNodeHandle>();
		}

		TArray<FConstNodeHandle> FGraphController::GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc) const
		{
			if (GraphClassPtr.IsValid())
			{
				return GetNodeHandles(GetNodesAndClassesByPredicate(InFilterFunc));
			}
			return TArray<FConstNodeHandle>();
		}

		TArray<FNodeHandle> FGraphController::GetNodeHandles(TArrayView<const FGraphController::FNodeAndClass> InNodesAndClasses)
		{
			TArray<FNodeHandle> Nodes;

			for (const FNodeAndClass& NodeAndClass: InNodesAndClasses)
			{
				FNodeHandle NodeController = GetNodeHandle(NodeAndClass);
				if (NodeController->IsValid())
				{
					Nodes.Add(NodeController);
				}
			}

			return Nodes;
		}

		TArray<FConstNodeHandle> FGraphController::GetNodeHandles(TArrayView<const FGraphController::FConstNodeAndClass> InNodesAndClasses) const
		{
			TArray<FConstNodeHandle> Nodes;

			for (const FConstNodeAndClass& NodeAndClass : InNodesAndClasses)
			{
				FConstNodeHandle NodeController = GetNodeHandle(NodeAndClass);
				if (NodeController->IsValid())
				{
					Nodes.Add(NodeController);
				}
			}

			return Nodes;
		}

		FNodeHandle FGraphController::GetNodeHandle(const FGraphController::FNodeAndClass& InNodeAndClass)
		{
			if (InNodeAndClass.IsValid() && GraphClassPtr.IsValid())
			{
				FGraphHandle OwningGraph = this->AsShared();
				FGraphAccessPtr GraphPtr = GraphClassPtr.GetGraph();

				switch (InNodeAndClass.Class->Metadata.Type)
				{
					case EMetasoundFrontendClassType::Input:
						if (FClassInputAccessPtr OwningGraphClassInputPtr = FindInputDescriptionWithNodeID(InNodeAndClass.Node->ID))
						{
							FInputNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								OwningGraphClassInputPtr,
								GraphPtr,
								OwningGraph
							};
							return FInputNodeController::CreateInputNodeHandle(InitParams);
						}
						else
						{
							// TODO: This supports input nodes introduced during subgraph inflation. Input nodes
							// should be replaced with value nodes once they are implemented. 
							FNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FNodeController::CreateNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::Output:
						if (FClassOutputAccessPtr OwningGraphClassOutputPtr = FindOutputDescriptionWithNodeID(InNodeAndClass.Node->ID))
						{
							FOutputNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								OwningGraphClassOutputPtr,
								GraphPtr,
								OwningGraph
							};
							return FOutputNodeController::CreateOutputNodeHandle(InitParams);
						}
						else
						{
							// TODO: This supports output nodes introduced during subgraph inflation. Output nodes
							// should be replaced with value nodes once they are implemented. 
							FNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FNodeController::CreateNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::External:
						{
							FNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FNodeController::CreateNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::Graph:
						{
							FSubgraphNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FSubgraphNodeController::CreateNodeHandle(InitParams);
						}
						break;

					default:
						checkNoEntry();
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FGraphController::GetNodeHandle(const FGraphController::FConstNodeAndClass& InNodeAndClass) const
		{
			if (InNodeAndClass.IsValid() && GraphClassPtr.IsValid())
			{
				FConstGraphHandle OwningGraph = this->AsShared();
				FConstGraphAccessPtr GraphPtr = GraphClassPtr.GetGraph();

				switch (InNodeAndClass.Class->Metadata.Type)
				{
					case EMetasoundFrontendClassType::Input:
						if (FConstClassInputAccessPtr OwningGraphClassInputPtr = FindInputDescriptionWithNodeID(InNodeAndClass.Node->ID))
						{
							FInputNodeController::FInitParams InitParams
							{
								ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
								InNodeAndClass.Class,
								ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr),
								ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
								ConstCastSharedRef<IGraphController>(OwningGraph)
							};
							return FInputNodeController::CreateConstInputNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::Output:
						if (FConstClassOutputAccessPtr OwningGraphClassOutputPtr = FindOutputDescriptionWithNodeID(InNodeAndClass.Node->ID))
						{
							FOutputNodeController::FInitParams InitParams
							{
								ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
								InNodeAndClass.Class,
								ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr),
								ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
								ConstCastSharedRef<IGraphController>(OwningGraph)
							};
							return FOutputNodeController::CreateConstOutputNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::External:
					case EMetasoundFrontendClassType::Graph:
						{
							FNodeController::FInitParams InitParams
							{
								ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
								InNodeAndClass.Class,
								ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
								ConstCastSharedRef<IGraphController>(OwningGraph)
							};
							return FNodeController::CreateConstNodeHandle(InitParams);
						}
						break;

					default:
						checkNoEntry();
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithName(const FString& InName)
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		const FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithName(const FString& InName) const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithName(const FString& InName)
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		const FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithName(const FString& InName) const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithVertexID(const FGuid& InVertexID)
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		const FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithVertexID(const FGuid& InVertexID) const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithVertexID(const FGuid& InVertexID)
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		const FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithVertexID(const FGuid& InVertexID) const
		{
			if (GraphClassPtr.IsValid())
			{
				return GraphClassPtr->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		FClassInputAccessPtr FGraphController::FindInputDescriptionWithNodeID(FGuid InNodeID)
		{
			return GraphClassPtr.GetInputWithNodeID(InNodeID);
		}

		FConstClassInputAccessPtr FGraphController::FindInputDescriptionWithNodeID(FGuid InNodeID) const
		{
			return GraphClassPtr.GetInputWithNodeID(InNodeID);
		}

		FClassOutputAccessPtr FGraphController::FindOutputDescriptionWithNodeID(FGuid InNodeID)
		{
			return GraphClassPtr.GetOutputWithNodeID(InNodeID);
		}

		FConstClassOutputAccessPtr FGraphController::FindOutputDescriptionWithNodeID(FGuid InNodeID) const
		{
			return GraphClassPtr.GetOutputWithNodeID(InNodeID);
		}

		FDocumentAccess FGraphController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.GraphClass = GraphClassPtr;
			Access.ConstGraphClass = GraphClassPtr;

			return Access;
		}

		FConstDocumentAccess FGraphController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstGraphClass = GraphClassPtr;

			return Access;
		}


		//
		// FDocumentController
		//
		FDocumentController::FDocumentController(FDocumentAccessPtr InDocumentPtr)
		:	DocumentPtr(InDocumentPtr)
		{
		}

		bool FDocumentController::IsValid() const
		{
			return DocumentPtr.IsValid();
		}

		TArray<FMetasoundFrontendClass> FDocumentController::GetDependencies() const
		{
			if (DocumentPtr.IsValid())
			{
				return DocumentPtr->Dependencies;
			}
			return TArray<FMetasoundFrontendClass>();
		}

		TArray<FMetasoundFrontendGraphClass> FDocumentController::GetSubgraphs() const
		{
			if (DocumentPtr.IsValid())
			{
				return DocumentPtr->Subgraphs;
			}
			return TArray<FMetasoundFrontendGraphClass>();
		}

		TArray<FMetasoundFrontendClass> FDocumentController::GetClasses() const 
		{
			TArray<FMetasoundFrontendClass> Classes = GetDependencies();
			Classes.Append(GetSubgraphs());
			return Classes;
		}

		bool FDocumentController::AddDuplicateSubgraph(const FMetasoundFrontendGraphClass& InGraphToCopy, const FMetasoundFrontendDocument& InOtherDocument)
		{
			FMetasoundFrontendDocument* ThisDocument = DocumentPtr.Get();
			if (nullptr == ThisDocument)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add subgraph to invalid document"));
				return false;
			}

			// Direct copy of subgraph
			bool bSuccess = true;
			FMetasoundFrontendGraphClass SubgraphCopy(InGraphToCopy);

			for (FMetasoundFrontendNode& Node : SubgraphCopy.Graph.Nodes)
			{
				const FGuid OriginalClassID = Node.ClassID;

				auto IsClassWithClassID = [&](const FMetasoundFrontendClass& InClass) -> bool
				{
					return InClass.ID == OriginalClassID;
				};

				if (const FMetasoundFrontendClass* OriginalNodeClass = InOtherDocument.Dependencies.FindByPredicate(IsClassWithClassID))
				{
					// Should not be a graph class since it's in the dependencies list
					check(EMetasoundFrontendClassType::Graph != OriginalNodeClass->Metadata.Type);

					if (const FMetasoundFrontendClass* NodeClass = FindOrAddClass(OriginalNodeClass->Metadata).Get())
					{
						// All this just to update this ID. Maybe having globally 
						// consistent class IDs would help. Or using the classname & version as
						// a class ID. 
						Node.ClassID = NodeClass->ID;
					}
					else
					{
						UE_LOG(LogMetaSound, Error, TEXT("Failed to add subgraph dependency [Class:%s]"), *OriginalNodeClass->Metadata.ClassName.ToString());
						bSuccess = false;
					}
				}
				else if (const FMetasoundFrontendGraphClass* OriginalNodeGraphClass = InOtherDocument.Subgraphs.FindByPredicate(IsClassWithClassID))
				{
					bSuccess = bSuccess && AddDuplicateSubgraph(*OriginalNodeGraphClass, InOtherDocument);
					if (!bSuccess)
					{
						break;
					}
				}
				else
				{
					bSuccess = false;
					UE_LOG(LogMetaSound, Error, TEXT("Failed to copy subgraph. Subgraph document is missing dependency info for node [Node:%s, NodeID:%s]"), *Node.Name, *Node.ID.ToString());
				}
			}

			if (bSuccess)
			{
				ThisDocument->Subgraphs.Add(SubgraphCopy);
			}

			return bSuccess;
		}

		FGraphHandle FDocumentController::AddDuplicateSubgraph(const IGraphController& InGraph)
		{
			// TODO: class IDs have issues.. 
			// Currently ClassIDs are just used for internal linking. They need to be fixed up
			// here if swapping documents. In the future, ClassIDs should be unique and consistent
			// across documents and platforms.

			FConstDocumentAccess GraphDocumentAccess = GetSharedAccess(*InGraph.GetOwningDocument());
			const FMetasoundFrontendDocument* OtherDocument = GraphDocumentAccess.ConstDocument.Get();
			if (nullptr == OtherDocument)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add subgraph from invalid document"));
				return IGraphController::GetInvalidHandle();
			}

			FConstDocumentAccess GraphAccess = GetSharedAccess(InGraph);
			const FMetasoundFrontendGraphClass* OtherGraph = GraphAccess.ConstGraphClass.Get();
			if (nullptr == OtherGraph)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add invalid subgraph to document"));
				return IGraphController::GetInvalidHandle();
			}

			if (AddDuplicateSubgraph(*OtherGraph, *OtherDocument))
			{
				if (const FMetasoundFrontendClass* SubgraphClass = FindClass(OtherGraph->Metadata).Get())
				{
					return GetSubgraphWithClassID(SubgraphClass->ID);
				}
			}

			return IGraphController::GetInvalidHandle();
		}
 

		FConstClassAccessPtr FDocumentController::FindDependencyWithID(FGuid InClassID) const 
		{
			return DocumentPtr.GetDependencyWithID(InClassID);
		}

		FConstGraphClassAccessPtr FDocumentController::FindSubgraphWithID(FGuid InClassID) const
		{
			return DocumentPtr.GetSubgraphWithID(InClassID);
		}

		FConstClassAccessPtr FDocumentController::FindClassWithID(FGuid InClassID) const
		{
			FConstClassAccessPtr MetasoundClass = FindDependencyWithID(InClassID);

			if (!MetasoundClass.IsValid())
			{
				MetasoundClass = FindSubgraphWithID(InClassID);
			}

			return MetasoundClass;
		}

		void FDocumentController::SetMetadata(const FMetasoundFrontendDocumentMetadata& InMetadata)
		{
			if (DocumentPtr.IsValid())
			{
				DocumentPtr->Metadata = InMetadata;
			}
		}

		const FMetasoundFrontendDocumentMetadata& FDocumentController::GetMetadata() const
		{
			if (DocumentPtr.IsValid())
			{
				return DocumentPtr->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendDocumentMetadata>();
		}

		FConstClassAccessPtr FDocumentController::FindClass(const FNodeRegistryKey& InKey) const
		{
			return DocumentPtr.GetClassWithRegistryKey(InKey);
		}

		FConstClassAccessPtr FDocumentController::FindOrAddClass(const FNodeRegistryKey& InKey)
		{
			FConstClassAccessPtr ClassPtr = FindClass(InKey);

			auto AddClass = [=](FMetasoundFrontendClass&& NewClassDescription)
			{
				FConstClassAccessPtr NewClassPtr;
				if (DocumentPtr.IsValid())
				{
					// Cannot add a subgraph using this method because dependencies
					// of external graph are not added in this method.
					check(EMetasoundFrontendClassType::Graph != NewClassDescription.Metadata.Type);
					NewClassDescription.ID = FGuid::NewGuid();

					DocumentPtr->Dependencies.Add(MoveTemp(NewClassDescription));
					NewClassPtr = FindClass(InKey);
				}
				return NewClassPtr;
			};

			if (const FMetasoundFrontendClass* MetasoundClass = ClassPtr.Get())
			{
				// External node classes must match version to return shared definition.
				if (MetasoundClass->Metadata.Type == EMetasoundFrontendClassType::External)
				{
					// TODO: Assuming we want to recheck classes when they add another
					// node, this should be replace with a call to synchronize a 
					// single class.
					FMetasoundFrontendClass NewClass = GenerateClassDescription(InKey);
					if (NewClass.Metadata.Version.Major != ClassPtr->Metadata.Version.Major)
					{
						return AddClass(MoveTemp(NewClass));
					}
				}

				return ClassPtr;
			}

			FMetasoundFrontendClass NewClass = GenerateClassDescription(InKey);
			return AddClass(MoveTemp(NewClass));
		}

		FConstClassAccessPtr FDocumentController::FindClass(const FMetasoundFrontendClassMetadata& InMetadata) const
		{
			return DocumentPtr.GetClassWithMetadata(InMetadata);
		}

		FConstClassAccessPtr FDocumentController::FindOrAddClass(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			FConstClassAccessPtr ClassPtr = FindClass(InMetadata);

			if (!DocumentPtr.IsValid())
			{
				return ClassPtr;
			}

			// External node classes must match major version to return shared definition.
			if (EMetasoundFrontendClassType::External == InMetadata.Type)
			{
				if (ClassPtr.IsValid())
				{
					if (InMetadata.Version.Major != ClassPtr->Metadata.Version.Major)
					{
						ClassPtr = FConstClassAccessPtr();
					}
				}
			}

			if (!ClassPtr.IsValid())
			{
				if ((EMetasoundFrontendClassType::External == InMetadata.Type) || (EMetasoundFrontendClassType::Input == InMetadata.Type) || (EMetasoundFrontendClassType::Output == InMetadata.Type))
				{
					FMetasoundFrontendClass NewClass;
					FNodeRegistryKey Key = FRegistry::GetRegistryKey(InMetadata);

					if (FRegistry::GetFrontendClassFromRegistered(Key, NewClass))
					{
						NewClass.ID = FGuid::NewGuid();
						DocumentPtr->Dependencies.Add(NewClass);
					}
					else
					{
						UE_LOG(LogMetaSound, Error,
							TEXT("Cannot add external dependency. No Metasound class found with matching registry key [Key:%s, Name:%s, Version:%s]. Suggested solution \"%s\" by %s."),
							*Key,
							*InMetadata.ClassName.GetFullName().ToString(),
							*InMetadata.Version.ToString(),
							*InMetadata.PromptIfMissing.ToString(),
							*InMetadata.Author.ToString());
					}
				}
				else if (EMetasoundFrontendClassType::Graph == InMetadata.Type)
				{
					FMetasoundFrontendGraphClass NewClass;
					NewClass.ID = FGuid::NewGuid();
					NewClass.Metadata = InMetadata;

					DocumentPtr->Subgraphs.Add(NewClass);
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT(
						"Unsupported metasound class type for node: \"%s\" (%s)."),
						*InMetadata.ClassName.GetFullName().ToString(),
						*InMetadata.Version.ToString());
					checkNoEntry();
				}

				ClassPtr = FindClass(InMetadata);
			}

			return ClassPtr;
		}

		void FDocumentController::SynchronizeDependencies()
		{
			if (!DocumentPtr.IsValid())
			{
				return;
			}

			int32 NumDependenciesRemovedThisItr = 0;

			// Repeatedly remove unreferenced dependencies until there are
			// no unreferenced dependencies left.
			do
			{
				TSet<FGuid> ReferencedDependencyIDs;
				auto AddNodeClassIDToSet = [&](const FMetasoundFrontendNode& Node)
				{
					ReferencedDependencyIDs.Add(Node.ClassID);
				};

				auto AddGraphNodeClassIDsToSet = [&](const FMetasoundFrontendGraphClass& GraphClass)
				{
					Algo::ForEach(GraphClass.Graph.Nodes, AddNodeClassIDToSet);
				};

				// Referenced dependencies in root class
				Algo::ForEach(DocumentPtr->RootGraph.Graph.Nodes, AddNodeClassIDToSet);

				// Referenced dependencies in subgraphs
				Algo::ForEach(DocumentPtr->Subgraphs, AddGraphNodeClassIDsToSet);

				auto IsDependencyUnreferenced = [&](const FMetasoundFrontendClass& ClassDependency)
				{
					return !ReferencedDependencyIDs.Contains(ClassDependency.ID);
				};

				NumDependenciesRemovedThisItr = DocumentPtr->Dependencies.RemoveAllSwap(IsDependencyUnreferenced);
			}
			while (NumDependenciesRemovedThisItr > 0);

			/*
			FRegistry* Registry = FRegistry::Get();
			if (ensure(Registry))
			{
				// Make sure classes are up-to-date with registered versions of class
				for (FMetasoundFrontendClass& Class : DocumentPtr->Dependencies)
				{
					FMetasoundFrontendClass RegisteredClass;
					FNodeRegistryKey Key = FRegistry::GetRegistryKey(Class.Metadata);
					if (Registry->FindFrontendClassFromRegistered(Key, RegisteredClass))
					{
						Class = RegisteredClass;
						// TODO: update nodes for class.
						// TODO: perform minor version updating.
					}
				}
			}
			*/
		}

		FGraphHandle FDocumentController::GetRootGraph()
		{
			if (DocumentPtr.IsValid())
			{
				FGraphClassAccessPtr GraphClass = DocumentPtr.GetRootGraph();
				return FGraphController::CreateGraphHandle(FGraphController::FInitParams{GraphClass, this->AsShared()});
			}

			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FDocumentController::GetRootGraph() const
		{
			if (DocumentPtr.IsValid())
			{
				FConstGraphClassAccessPtr GraphClass = DocumentPtr.GetRootGraph();
				return FGraphController::CreateConstGraphHandle(FGraphController::FInitParams
					{
						ConstCastAccessPtr<FGraphClassAccessPtr>(GraphClass),
						ConstCastSharedRef<IDocumentController>(this->AsShared())
					});
			}
			return FInvalidGraphController::GetInvalid();
		}

		TArray<FGraphHandle> FDocumentController::GetSubgraphHandles() 
		{
			TArray<FGraphHandle> Subgraphs;

			if (DocumentPtr.IsValid())
			{
				for (FMetasoundFrontendGraphClass& GraphClass : DocumentPtr->Subgraphs)
				{
					Subgraphs.Add(GetSubgraphWithClassID(GraphClass.ID));
				}
			}

			return Subgraphs;
		}

		TArray<FConstGraphHandle> FDocumentController::GetSubgraphHandles() const 
		{
			TArray<FConstGraphHandle> Subgraphs;

			if (DocumentPtr.IsValid())
			{
				for (const FMetasoundFrontendGraphClass& GraphClass : DocumentPtr->Subgraphs)
				{
					Subgraphs.Add(GetSubgraphWithClassID(GraphClass.ID));
				}
			}

			return Subgraphs;
		}

		FGraphHandle FDocumentController::GetSubgraphWithClassID(FGuid InClassID)
		{
			FGraphClassAccessPtr GraphClassPtr = DocumentPtr.GetSubgraphWithID(InClassID);

			return FGraphController::CreateGraphHandle(FGraphController::FInitParams{GraphClassPtr, this->AsShared()});
		}

		FConstGraphHandle FDocumentController::GetSubgraphWithClassID(FGuid InClassID) const
		{
			FConstGraphClassAccessPtr GraphClassPtr = DocumentPtr.GetSubgraphWithID(InClassID);

			return FGraphController::CreateConstGraphHandle(FGraphController::FInitParams{ConstCastAccessPtr<FGraphClassAccessPtr>(GraphClassPtr), ConstCastSharedRef<IDocumentController>(this->AsShared())});
		}

		bool FDocumentController::ExportToJSONAsset(const FString& InAbsolutePath) const
		{
			if (DocumentPtr.IsValid())
			{
				if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
				{
					TJsonStructSerializerBackend<DefaultCharType> Backend(*FileWriter, EStructSerializerBackendFlags::Default);
					FStructSerializer::Serialize<FMetasoundFrontendDocument>(*DocumentPtr, Backend);
			
					FileWriter->Close();

					return true;
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to export Metasound json asset. Could not write to path \"%s\"."), *InAbsolutePath);
				}
			}

			return false;
		}

		FString FDocumentController::ExportToJSON() const
		{
			TArray<uint8> WriterBuffer;
			FMemoryWriter MemWriter(WriterBuffer);

			Metasound::TJsonStructSerializerBackend<Metasound::DefaultCharType> Backend(MemWriter, EStructSerializerBackendFlags::Default);
			FStructSerializer::Serialize<FMetasoundFrontendDocument>(*DocumentPtr, Backend);

			MemWriter.Close();

			// null terminator
			WriterBuffer.AddZeroed(sizeof(ANSICHAR));

			FString Output;
			Output.AppendChars(reinterpret_cast<ANSICHAR*>(WriterBuffer.GetData()), WriterBuffer.Num() / sizeof(ANSICHAR));

			return Output;
		}
		
		bool FDocumentController::IsMatchingMetasoundClass(const FMetasoundFrontendClassMetadata& InMetadataA, const FMetasoundFrontendClassMetadata& InMetadataB) 
		{
			if (InMetadataA.Type == InMetadataB.Type)
			{
				if (InMetadataA.ClassName == InMetadataB.ClassName)
				{
					return FRegistry::GetRegistryKey(InMetadataA) == FRegistry::GetRegistryKey(InMetadataB);
				}
			}
			return false;
		}

		bool FDocumentController::IsMatchingMetasoundClass(const FNodeClassInfo& InNodeClass, const FMetasoundFrontendClassMetadata& InMetadata) 
		{
			return FRegistry::GetRegistryKey(InNodeClass) == FRegistry::GetRegistryKey(InMetadata);
		}

		bool FDocumentController::IsMatchingMetasoundClass(const FNodeRegistryKey& InKey, const FMetasoundFrontendClassMetadata& InMetadata) 
		{
			return InKey == FRegistry::GetRegistryKey(InMetadata);
		}

		FDocumentAccess FDocumentController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.Document = DocumentPtr;
			Access.ConstDocument = DocumentPtr;

			return Access;
		}

		FConstDocumentAccess FDocumentController::ShareAccess() const 
		{
			FConstDocumentAccess Access;

			Access.ConstDocument = DocumentPtr;

			return Access;
		}
	}
}
#undef LOCTEXT_NAMESPACE
