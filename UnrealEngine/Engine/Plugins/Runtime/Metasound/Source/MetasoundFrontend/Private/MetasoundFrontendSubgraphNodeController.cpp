// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSubgraphNodeController.h"

#include "Algo/Transform.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace FrontendSubgraphNodeControllerPrivate
		{
			template<typename SetAType, typename SetBType>
			void InplaceBidirectionalVertexSetDifference(TArray<const SetAType*>& SetA, TArray<const SetBType*>& SetB)
			{
				// TODO: comparison is done by `Name`. May update to a `ClassVertexID` in the future. 
				// Reverse iterate to allow removal of elements from SetA within loop.
				for (int32 SetAIndex = (SetA.Num() - 1); SetAIndex >= 0; SetAIndex--) 
				{
					const FString& SetAName = SetA[SetAIndex]->Name;
					int32 SetBIndex = SetB.IndexOfByPredicate([&](const SetBType* InVertex) { return InVertex->Name == SetAName; });
					if (INDEX_NONE != SetBIndex)
					{
						SetA.RemoveAt(SetAIndex);
						SetB.RemoveAt(SetBIndex);
					}
				}
			}

			// Manipulates the NodeVertexArray to match the ClassVertexArray.
			template<typename NodeVertexType, typename ClassVertexType>
			bool ConformNodeVertexArrayToClassVertexArray(TArray<NodeVertexType>& NodeVertexArray, const TArray<ClassVertexType>& ClassVertexArray)
			{
				// TODO: May need to change this to support dynamic pins. 
				bool bDidAlterNodeVertexArray = false;

				TArray<const NodeVertexType*> NodeVerticesToRemove;
				Algo::Transform(NodeVertexArray, NodeVerticesToRemove, [](const NodeVertexType& Vertex) { return &Vertex; });

				TArray<const ClassVertexType*> ClassVerticesToAdd;
				Algo::Transform(ClassVertexArray, ClassVerticesToAdd, [](const ClassVertexType& Vertex) { return &Vertex; });
			
				FrontendSubgraphNodeControllerPrivate::InplaceBidirectionalVertexSetDifference(NodeVerticesToRemove, ClassVerticesToAdd);

				bDidAlterNodeVertexArray = (NodeVerticesToRemove.Num() > 0) || (ClassVerticesToAdd.Num() > 0);

				// Remove extra node vertices.
				for (const NodeVertexType* VertexToRemove : NodeVerticesToRemove)
				{
					NodeVertexArray.RemoveAll([&](const NodeVertexType& InVertex) { return InVertex.VertexID == VertexToRemove->VertexID; });
				}

				// Add missing node vertices. 
				for (const ClassVertexType* VertexToAdd : ClassVerticesToAdd)
				{
					NodeVertexType NewNodeVertex(*VertexToAdd);
					NewNodeVertex.VertexID = FGuid::NewGuid();
					NodeVertexArray.Add(NewNodeVertex);
				}

				return bDidAlterNodeVertexArray;
			}
		}

		// 
		// FSubgraphNodeController
		//
		FSubgraphNodeController::FSubgraphNodeController(EPrivateToken InToken, const FSubgraphNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, GraphPtr(InParams.GraphPtr)
		{
		}

		FNodeHandle FSubgraphNodeController::CreateNodeHandle(const FSubgraphNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				// Cannot make a valid node handle if the node description and class description differ
				if (InParams.NodePtr->ClassID == InParams.ClassPtr->ID)
				{
					return MakeShared<FSubgraphNodeController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FSubgraphNodeController::CreateConstNodeHandle(const FSubgraphNodeController::FInitParams& InParams)
		{
			if (InParams.NodePtr.IsValid() && InParams.ClassPtr.IsValid())
			{
				// Cannot make a valid node handle if the node description and class description differ
				if (InParams.NodePtr->ClassID == InParams.ClassPtr->ID)
				{
					return MakeShared<const FSubgraphNodeController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *InParams.NodePtr->ID.ToString(), *InParams.NodePtr->ClassID.ToString(), *InParams.ClassPtr->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		bool FSubgraphNodeController::IsValid() const
		{
			return FBaseNodeController::IsValid() && GraphPtr.IsValid();
		}

		int32 FSubgraphNodeController::GetNumInputs() const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetNumInputs();
		}

		int32 FSubgraphNodeController::GetNumOutputs() const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetNumOutputs();
		}

		TArray<FBaseNodeController::FInputControllerParams> FSubgraphNodeController::GetInputControllerParams() const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetInputControllerParams();
		}

		TArray<FBaseNodeController::FOutputControllerParams> FSubgraphNodeController::GetOutputControllerParams() const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetOutputControllerParams();
		}

		TArray<FBaseNodeController::FInputControllerParams> FSubgraphNodeController::GetInputControllerParamsWithVertexName(const FString& InName) const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetInputControllerParamsWithVertexName(InName);
		}

		TArray<FBaseNodeController::FOutputControllerParams> FSubgraphNodeController::GetOutputControllerParamsWithVertexName(const FString& InName) const
		{
			TArray<FBaseNodeController::FOutputControllerParams> Outputs;

			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::GetOutputControllerParamsWithVertexName(InName);
		}

		bool FSubgraphNodeController::FindInputControllerParamsWithID(FGuid InVertexID, FInputControllerParams& OutParams) const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::FindInputControllerParamsWithID(InVertexID, OutParams);
		}

		bool FSubgraphNodeController::FindOutputControllerParamsWithID(FGuid InVertexID, FOutputControllerParams& OutParams) const
		{
			// TODO: Trigger ConformNodeInterfaceToClassInterface() on an
			// event callback rather than on every call to this function.
			const_cast<FSubgraphNodeController*>(this)->ConformNodeInterfaceToClassInterface();

			return Super::FindOutputControllerParamsWithID(InVertexID, OutParams);
		}

		void FSubgraphNodeController::ConformNodeInterfaceToClassInterface() 
		{
			using namespace FrontendSubgraphNodeControllerPrivate;

			// TODO: will need to update this logic to handle external classes and 
			// dynamic interfaces. These updates will get piped down to the INodeClass
			// interface.
			if (FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* NodeClass = ClassPtr.Get())
				{
					ConformNodeVertexArrayToClassVertexArray(Node->Interface.Inputs, NodeClass->Interface.Inputs);
					ConformNodeVertexArrayToClassVertexArray(Node->Interface.Outputs, NodeClass->Interface.Outputs);
					// TODO: Conform environment variables by aggregating all dependent environment variables.
				}
			}
		}

		FDocumentAccess FSubgraphNodeController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FSubgraphNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FInputHandle FSubgraphNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseInputController>(FBaseInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FSubgraphNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseOutputController>(FBaseOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, GraphPtr, InOwningNode});
		}
	}
}
