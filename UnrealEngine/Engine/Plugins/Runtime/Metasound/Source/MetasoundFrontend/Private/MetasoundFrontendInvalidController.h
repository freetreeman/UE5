// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundGraph.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundFrontendInvalidControllerPrivate
		{
			// Helper to create a functional local static. Used when the interface requires
			// a const reference returned.
			template<typename Type>
			const Type& GetInvalid()
			{
				static const Type InvalidValue;
				return InvalidValue;
			}
		}

		/** FInvalidOutputController is a controller which is always invalid. 
		 *
		 *  All methods return defaults and return error flags or invalid values.
		 */
		class METASOUNDFRONTEND_API FInvalidOutputController : public IOutputController
		{
		public:
			FInvalidOutputController() = default;

			virtual ~FInvalidOutputController() = default;

			static TSharedRef<IOutputController> GetInvalid()
			{
				static TSharedRef<IOutputController> InvalidController = MakeShared<FInvalidOutputController>();
				return MakeShared<FInvalidOutputController>();
			}

			bool IsValid() const override { return false; }
			FGuid GetID() const override { return Metasound::FrontendInvalidID; }
			const FName& GetDataType() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FName>(); }
			const FString& GetName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FString>(); }
			const FText& GetDisplayName() const override { return FText::GetEmpty(); }
			const FText& GetTooltip() const override { return FText::GetEmpty(); }
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendVertexMetadata>(); }
			FGuid GetOwningNodeID() const override { return Metasound::FrontendInvalidID; }
			TSharedRef<INodeController> GetOwningNode() override;
			TSharedRef<const INodeController> GetOwningNode() const override;

			bool IsConnected() const override { return false; }
			TArray<FInputHandle> GetConnectedInputs() override { return TArray<FInputHandle>(); }
			TArray<FConstInputHandle> GetConstConnectedInputs() const override { return TArray<FConstInputHandle>(); }
			bool Disconnect() override { return false; }

			FConnectability CanConnectTo(const IInputController& InController) const override { return FConnectability(); }
			bool Connect(IInputController& InController) override { return false; }
			bool ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName) override { return false; }
			bool Disconnect(IInputController& InController) override { return false; }

		protected:
			FDocumentAccess ShareAccess() override { return FDocumentAccess(); }
			FConstDocumentAccess ShareAccess() const override { return FConstDocumentAccess(); }

		};

		/** FInvalidInputController is a controller which is always invalid. 
		 *
		 *  All methods return defaults and return error flags or invalid values.
		 */
		class METASOUNDFRONTEND_API FInvalidInputController : public IInputController 
		{
		public:
			FInvalidInputController() = default;
			virtual ~FInvalidInputController() = default;

			static TSharedRef<IInputController> GetInvalid()
			{
				static TSharedRef<IInputController> InvalidController = MakeShared<FInvalidInputController>();
				return InvalidController;
			}

			bool IsValid() const override { return false; }
			FGuid GetID() const override { return Metasound::FrontendInvalidID; }
			bool IsConnected() const override { return false; }
			const FName& GetDataType() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FName>(); }
			const FString& GetName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FString>(); }
			const FText& GetDisplayName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }
			const FText& GetTooltip() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendVertexMetadata>(); }
			const FMetasoundFrontendLiteral* GetLiteral() const override { return nullptr; }
			void SetLiteral(const FMetasoundFrontendLiteral& InLiteral) { };
			const FMetasoundFrontendLiteral* GetClassDefaultLiteral() const override { return nullptr; }
			FGuid GetOwningNodeID() const override { return Metasound::FrontendInvalidID; }
			TSharedRef<INodeController> GetOwningNode() override;
			TSharedRef<const INodeController> GetOwningNode() const override;

			virtual TSharedRef<IOutputController> GetConnectedOutput() override { return FInvalidOutputController::GetInvalid(); }
			virtual TSharedRef<const IOutputController> GetConnectedOutput() const override { return FInvalidOutputController::GetInvalid(); }
			bool Disconnect() override { return false; }

			virtual FConnectability CanConnectTo(const IOutputController& InController) const override { return FConnectability(); }
			virtual bool Connect(IOutputController& InController) override { return false; }
			virtual bool ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName) override { return false; }
			virtual bool Disconnect(IOutputController& InController) override { return false; }
		protected:
			FDocumentAccess ShareAccess() override { return FDocumentAccess(); }
			FConstDocumentAccess ShareAccess() const override { return FConstDocumentAccess(); }
		};

		/** FInvalidNodeController is a controller which is always invalid. 
		 *
		 *  All methods return defaults and return error flags or invalid values.
		 */
		class METASOUNDFRONTEND_API FInvalidNodeController : public INodeController 
		{

		public:
			FInvalidNodeController() = default;
			virtual ~FInvalidNodeController() = default;

			static TSharedRef<INodeController> GetInvalid()
			{
				static TSharedRef<INodeController> InvalidValue = MakeShared<FInvalidNodeController>();
				return InvalidValue;
			}

			bool IsValid() const override { return false; }

			TArray<TSharedRef<IInputController>> GetInputs() override { return TArray<TSharedRef<IInputController>>(); }
			TArray<TSharedRef<IOutputController>> GetOutputs() override { return TArray<TSharedRef<IOutputController>>(); }
			TArray<TSharedRef<const IInputController>> GetConstInputs() const override { return TArray<TSharedRef<const IInputController>>(); }
			TArray<TSharedRef<const IOutputController>> GetConstOutputs() const override { return TArray<TSharedRef<const IOutputController>>(); }

			TArray<FInputHandle> GetInputsWithVertexName(const FString& InName) override { return TArray<FInputHandle>(); }
			TArray<FConstInputHandle> GetConstInputsWithVertexName(const FString& InName) const override { return TArray<FConstInputHandle>(); }
			TArray<FOutputHandle> GetOutputsWithVertexName(const FString& InName) override { return TArray<FOutputHandle>(); }
			TArray<FConstOutputHandle> GetConstOutputsWithVertexName(const FString& InName) const override { return TArray<FConstOutputHandle>(); }
			TSharedRef<IInputController> GetInputWithID(FGuid InVertexID) override { return FInvalidInputController::GetInvalid(); }
			TSharedRef<IOutputController> GetOutputWithID(FGuid InVertexID) override { return FInvalidOutputController::GetInvalid(); }
			TSharedRef<const IInputController> GetInputWithID(FGuid InVertexID) const override { return FInvalidInputController::GetInvalid(); }
			TSharedRef<const IOutputController> GetOutputWithID(FGuid InVertexID) const override { return FInvalidOutputController::GetInvalid(); }

			const FMetasoundFrontendNodeStyle& GetNodeStyle() const override { static const FMetasoundFrontendNodeStyle Invalid; return Invalid; }
			void SetNodeStyle(const FMetasoundFrontendNodeStyle& InNodeStyle) { };

			bool CanAddInput(const FString& InVertexName) const override { return false; }
			FInputHandle AddInput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) override { return FInvalidInputController::GetInvalid(); }
			bool RemoveInput(FGuid InVertexID) override { return false; }

			bool CanAddOutput(const FString& InVertexName) const override { return false; }
			FInputHandle AddOutput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) override { return FInvalidInputController::GetInvalid(); }
			bool RemoveOutput(FGuid InVertexID) override { return false; }

			bool ClearInputLiteral(FGuid InVertexID) override { return false; };
			const FMetasoundFrontendLiteral* GetInputLiteral(const FGuid& InVertexID) const { return nullptr; }
			void SetInputLiteral(const FMetasoundFrontendVertexLiteral& InVertexLiteral) override { }

			const FMetasoundFrontendClassInterface& GetClassInterface() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendClassInterface>(); }
			const FMetasoundFrontendClassMetadata& GetClassMetadata() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendClassMetadata>(); }
			const FMetasoundFrontendInterfaceStyle& GetInputStyle() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendInterfaceStyle>(); }
			const FMetasoundFrontendInterfaceStyle& GetOutputStyle() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendInterfaceStyle>(); }
			const FMetasoundFrontendClassStyle& GetClassStyle() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendClassStyle>(); }

			const FText& GetDescription() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }

			bool IsRequired(const FMetasoundFrontendArchetype& InArchetype) const override { return false; }

			TSharedRef<IGraphController> AsGraph() override;
			TSharedRef<const IGraphController> AsGraph() const override;

			FGuid GetID() const override { return Metasound::FrontendInvalidID; }
			FGuid GetClassID() const override { return Metasound::FrontendInvalidID; }

			FGuid GetOwningGraphClassID() const override { return Metasound::FrontendInvalidID; }
			TSharedRef<IGraphController> GetOwningGraph() override;
			TSharedRef<const IGraphController> GetOwningGraph() const override;

			void IterateInputs(TUniqueFunction<void(FInputHandle)> InFunction) override { }
			void IterateConstInputs(TUniqueFunction<void(FConstInputHandle)> InFunction) const override { }

			void IterateOutputs(TUniqueFunction<void(FOutputHandle)> InFunction) override { }
			void IterateConstOutputs(TUniqueFunction<void(FConstOutputHandle)> InFunction) const override { }

			int32 GetNumInputs() const override { return 0; }
			int32 GetNumOutputs() const override { return 0; }

			const FString& GetNodeName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FString>(); }
			const FText&  GetDisplayName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }
			const FText& GetDisplayTitle() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }
			void SetDescription(const FText& InDescription) override { }
			void SetDisplayName(const FText& InText) override { }

		protected:
			FDocumentAccess ShareAccess() override { return FDocumentAccess(); }
			FConstDocumentAccess ShareAccess() const override { return FConstDocumentAccess(); }
		};

		/** FInvalidGraphController is a controller which is always invalid. 
		 *
		 *  All methods return defaults and return error flags or invalid values.
		 */
		class METASOUNDFRONTEND_API FInvalidGraphController : public IGraphController 
		{
			public:
			FInvalidGraphController() = default;
			virtual ~FInvalidGraphController() = default;

			static TSharedRef<IGraphController> GetInvalid()
			{
				static TSharedRef<IGraphController> InvalidGraph = MakeShared<FInvalidGraphController>();
				return InvalidGraph;
			}

			bool IsValid() const override { return false; }
			FGuid GetClassID() const override { return Metasound::FrontendInvalidID; }
			const FText& GetDisplayName() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FText>(); }

			TArray<FString> GetInputVertexNames() const override { return TArray<FString>(); }
			TArray<FString> GetOutputVertexNames() const override { return TArray<FString>(); }

			TArray<TSharedRef<INodeController>> GetNodes() override { return TArray<TSharedRef<INodeController>>(); }
			TArray<TSharedRef<const INodeController>> GetConstNodes() const override { return TArray<TSharedRef<const INodeController>>(); }

			TSharedRef<const INodeController> GetNodeWithID(FGuid InNodeID) const override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<INodeController> GetNodeWithID(FGuid InNodeID) override { return FInvalidNodeController::GetInvalid(); }

			TArray<TSharedRef<INodeController>> GetOutputNodes() override { return TArray<TSharedRef<INodeController>>(); }
			TArray<TSharedRef<INodeController>> GetInputNodes() override { return TArray<TSharedRef<INodeController>>(); }
			TArray<TSharedRef<const INodeController>> GetConstOutputNodes() const override { return TArray<TSharedRef<const INodeController>>(); }
			TArray<TSharedRef<const INodeController>> GetConstInputNodes() const override { return TArray<TSharedRef<const INodeController>>(); }

			const FMetasoundFrontendGraphStyle& GetGraphStyle() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendGraphStyle>(); }
			void SetGraphStyle(const FMetasoundFrontendGraphStyle& InStyle) override { }

			void IterateConstNodes(TUniqueFunction<void(FConstNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType /* = EMetasoundFrontendClassType::Invalid */) const override { }
			void IterateNodes(TUniqueFunction<void(FNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType /* = EMetasoundFrontendClassType::Invalid */) override { }

			bool ContainsOutputVertexWithName(const FString& InName) const override { return false; }
			bool ContainsInputVertexWithName(const FString& InName) const override { return false; }

			TSharedRef<const INodeController> GetOutputNodeWithName(const FString& InName) const override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<const INodeController> GetInputNodeWithName(const FString& InName) const override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<INodeController> GetOutputNodeWithName(const FString& InName) override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<INodeController> GetInputNodeWithName(const FString& InName) override { return FInvalidNodeController::GetInvalid(); }

			FConstClassInputAccessPtr FindClassInputWithName(const FString& InName) const override { return FConstClassInputAccessPtr(); }
			FConstClassOutputAccessPtr FindClassOutputWithName(const FString& InName) const override { return FConstClassOutputAccessPtr(); }

			TSharedRef<INodeController> AddInputVertex(const FMetasoundFrontendClassInput& InDescription) override { return FInvalidNodeController::GetInvalid(); }
			bool RemoveInputVertex(const FString& InputName) override { return false; }

			TSharedRef<INodeController> AddOutputVertex(const FMetasoundFrontendClassOutput& InDescription) override { return FInvalidNodeController::GetInvalid(); }
			bool RemoveOutputVertex(const FString& OutputName) override { return false; }

			// This can be used to determine what kind of property editor we should use for the data type of a given input.
			// Will return Invalid if the input couldn't be found, or if the input doesn't support any kind of literals.
			ELiteralType GetPreferredLiteralTypeForInputVertex(const FString& InInputName) const override { return ELiteralType::Invalid; }

			// For inputs whose preferred literal type is UObject or UObjectArray, this can be used to determine the UObject corresponding to that input's datatype.
			UClass* GetSupportedClassForInputVertex(const FString& InInputName) override { return nullptr; }

			FGuid GetVertexIDForInputVertex(const FString& InInputName) const { return Metasound::FrontendInvalidID; }
			FGuid GetVertexIDForOutputVertex(const FString& InInputName) const { return Metasound::FrontendInvalidID; }
			FMetasoundFrontendLiteral GetDefaultInput(const FGuid& InVertexID) const override { return FMetasoundFrontendLiteral{}; }

			// These can be used to set the default value for a given input on this graph.
			// @returns false if the input name couldn't be found, or if the literal type was incompatible with the Data Type of this input.
			bool SetDefaultInput(const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral) override { return false; }
			bool SetDefaultInputToDefaultLiteralOfType(const FGuid& InVertexID) override { return false; }

			const FText& GetInputDescription(const FString& InName) const override { return FText::GetEmpty(); }
			const FText& GetOutputDescription(const FString& InName) const override { return FText::GetEmpty(); }

			void SetInputDescription(const FString& InName, const FText& InDescription) override { }
			void SetOutputDescription(const FString& InName, const FText& InDescription) override { }
			void SetInputDisplayName(const FString& InName, const FText& InDisplayName) override { }
			void SetOutputDisplayName(const FString& InName, const FText& InDisplayName) override { }

			// This can be used to clear the current literal for a given input.
			// @returns false if the input name couldn't be found.
			bool ClearLiteralForInput(const FString& InInputName, FGuid InVertexID) override { return false; }

			TSharedRef<INodeController> AddNode(const FNodeRegistryKey& InNodeClass) override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<INodeController> AddNode(const FMetasoundFrontendClassMetadata& InNodeClass) override { return FInvalidNodeController::GetInvalid(); }
			TSharedRef<INodeController> AddDuplicateNode(const INodeController& InNode) override { return FInvalidNodeController::GetInvalid(); }

			// Remove the node corresponding to this node handle.
			// On success, invalidates the received node handle.
			bool RemoveNode(INodeController& InNode) override { return false; }

			// Returns the metadata for the current graph, including the name, description and author.
			const FMetasoundFrontendClassMetadata& GetGraphMetadata() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendClassMetadata>(); }

			void SetGraphMetadata(const FMetasoundFrontendClassMetadata& InMetadata) { }

			TSharedRef<INodeController> CreateEmptySubgraph(const FMetasoundFrontendClassMetadata& InInfo) override { return FInvalidNodeController::GetInvalid(); }

			TUniquePtr<IOperator> BuildOperator(const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<IOperatorBuilder::FBuildErrorPtr>& OutBuildErrors) const override
			{
				return TUniquePtr<IOperator>(nullptr);
			}

			TSharedRef<IDocumentController> GetOwningDocument() override;
			TSharedRef<const IDocumentController> GetOwningDocument() const override;
		protected:
			FDocumentAccess ShareAccess() override { return FDocumentAccess(); }
			FConstDocumentAccess ShareAccess() const override { return FConstDocumentAccess(); }
		};

		/** FInvalidDocumentController is a controller which is always invalid. 
		 *
		 *  All methods return defaults and return error flags or invalid values.
		 */
		class METASOUNDFRONTEND_API FInvalidDocumentController : public IDocumentController 
		{
			public:
				FInvalidDocumentController() = default;
				virtual ~FInvalidDocumentController() = default;

				static TSharedRef<IDocumentController> GetInvalid()
				{
					static TSharedRef<IDocumentController> InvalidDocument = MakeShared<FInvalidDocumentController>();
					return InvalidDocument;
				}

				bool IsValid() const override { return false; }

				TArray<FMetasoundFrontendClass> GetDependencies() const override { return TArray<FMetasoundFrontendClass>(); }
				TArray<FMetasoundFrontendGraphClass> GetSubgraphs() const override { return TArray<FMetasoundFrontendGraphClass>(); }
				TArray<FMetasoundFrontendClass> GetClasses() const override { return TArray<FMetasoundFrontendClass>(); }

				FConstClassAccessPtr FindDependencyWithID(FGuid InClassID) const override { return FConstClassAccessPtr(); }
				FConstGraphClassAccessPtr FindSubgraphWithID(FGuid InClassID) const override { return FConstGraphClassAccessPtr(); }
				FConstClassAccessPtr FindClassWithID(FGuid InClassID) const override { return FConstClassAccessPtr(); }

				FConstClassAccessPtr FindClass(const FNodeRegistryKey& InKey) const override { return FConstClassAccessPtr(); }
				FConstClassAccessPtr FindOrAddClass(const FNodeRegistryKey& InKey) override { return FConstClassAccessPtr(); }
				FConstClassAccessPtr FindClass(const FMetasoundFrontendClassMetadata& InMetadata) const override{ return FConstClassAccessPtr(); }
				FConstClassAccessPtr FindOrAddClass(const FMetasoundFrontendClassMetadata& InMetadata) override{ return FConstClassAccessPtr(); }
				FGraphHandle AddDuplicateSubgraph(const IGraphController& InGraph) override { return FInvalidGraphController::GetInvalid(); }

				void SetMetadata(const FMetasoundFrontendDocumentMetadata& InMetadata) override { }
				const FMetasoundFrontendDocumentMetadata& GetMetadata() const override { return MetasoundFrontendInvalidControllerPrivate::GetInvalid<FMetasoundFrontendDocumentMetadata>(); }

				void SynchronizeDependencies() override { }

				TArray<FGraphHandle> GetSubgraphHandles() override { return TArray<FGraphHandle>(); }

				TArray<FConstGraphHandle> GetSubgraphHandles() const override { return TArray<FConstGraphHandle>(); }

				FGraphHandle GetSubgraphWithClassID(FGuid InClassID) { return FInvalidGraphController::GetInvalid(); }

				FConstGraphHandle GetSubgraphWithClassID(FGuid InClassID) const { return FInvalidGraphController::GetInvalid(); }

				TSharedRef<IGraphController> GetRootGraph() override;
				TSharedRef<const IGraphController> GetRootGraph() const override;
				bool ExportToJSONAsset(const FString& InAbsolutePath) const override { return false; }
				FString ExportToJSON() const override { return FString(); }

			protected:

				FDocumentAccess ShareAccess() override { return FDocumentAccess(); }
				FConstDocumentAccess ShareAccess() const override { return FConstDocumentAccess(); }
		};
	}
}
