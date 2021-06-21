// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundNodeInterface.h"
#include "Misc/Guid.h"

#include "MetasoundFrontendDocument.generated.h"


// Forward Declarations
struct FMetasoundFrontendClass;
struct FMetasoundFrontendClassInterface;
namespace Metasound
{
	struct FLiteral;

	extern const FGuid METASOUNDFRONTEND_API FrontendInvalidID;
} // namespace Metasound


UENUM()
enum class EMetasoundFrontendClassType : uint8
{
	// The Metasound class is defined externally, in compiled code or in another document.
	External,

	// The Metasound class is a graph within the containing document.
	Graph,

	// The Metasound class is an input into a graph in the containing document.
	Input,

	// The Metasound class is an internal variable of a graph in the containing document.
	Variable,

	// The Metasound class is an output from a graph in the containing document.
	Output,

	Invalid UMETA(Hidden)
};

// General purpose version number for Metasound Frontend objects.
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendVersionNumber
{
	GENERATED_BODY()

	// Major version number.
	UPROPERTY(VisibleAnywhere, Category = General)
	int32 Major = 1;

	// Minor version number.
	UPROPERTY(VisibleAnywhere, Category = General)
	int32 Minor = 0;

	static const FMetasoundFrontendVersionNumber& GetInvalid()
	{
		static const FMetasoundFrontendVersionNumber Invalid { 0, 0 };
		return Invalid;
	}

	bool IsValid() const
	{
		return *this != GetInvalid();
	}

	friend bool operator==(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		return InLHS.Major == InRHS.Major && InLHS.Minor == InRHS.Minor;
	}

	friend bool operator!=(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		return InLHS.Major != InRHS.Major || InLHS.Minor != InRHS.Minor;
	}

	friend bool operator>(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		if (InLHS.Major > InRHS.Major)
		{
			return true;
		}

		if (InLHS.Major == InRHS.Major)
		{
			return InLHS.Minor > InRHS.Minor;
		}

		return false;
	}

	friend bool operator>=(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		return InLHS == InRHS || InLHS > InRHS;
	}

	friend bool operator<(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		if (InLHS.Major < InRHS.Major)
		{
			return true;
		}

		if (InLHS.Major == InRHS.Major)
		{
			return InLHS.Minor < InRHS.Minor;
		}

		return false;
	}

	friend bool operator<=(const FMetasoundFrontendVersionNumber& InLHS, const FMetasoundFrontendVersionNumber& InRHS)
	{
		return InLHS == InRHS || InLHS < InRHS;
	}

	FString ToString() const
	{
		return FString::Format(TEXT("v{0}.{1}"), { Major, Minor });
	}
};

// General purpose version info for Metasound Frontend objects.
USTRUCT()
struct FMetasoundFrontendVersion
{
	GENERATED_BODY()

	// Name of version.
	UPROPERTY()
	FName Name;

	// Version number.
	UPROPERTY()
	FMetasoundFrontendVersionNumber Number;
};


// An FMetasoundFrontendVertex provides a named connection point of a node.
USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendVertex
{
	GENERATED_BODY()

	virtual ~FMetasoundFrontendVertex() = default;

	// Name of the vertex. Unique amongst other vertices on the same interface.
	UPROPERTY(VisibleAnywhere, Category = CustomView)
	FString Name;

	// Data type name of the vertex.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FName TypeName;

	// ID of vertex
	UPROPERTY()	
	FGuid VertexID;

	// Returns true if vertices have equal name, type and number of IDs.
	static bool IsFunctionalEquivalent(const FMetasoundFrontendVertex& InLHS, const FMetasoundFrontendVertex& InRHS);
};

// Contains a default value for a single vertex ID
USTRUCT() 
struct FMetasoundFrontendVertexLiteral
{
	GENERATED_BODY()

	// ID of vertex.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FGuid VertexID = Metasound::FrontendInvalidID;

	// Value to use when constructing input. 
	UPROPERTY(EditAnywhere, Category = Parameters)
	FMetasoundFrontendLiteral Value;
};


USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendNodeInterface
{
	GENERATED_BODY()

	FMetasoundFrontendNodeInterface() = default;

	// Create a node interface which satisfies an existing class interface.
	FMetasoundFrontendNodeInterface(const FMetasoundFrontendClassInterface& InClassInterface);

	// Input vertices to node.
	UPROPERTY()
	TArray<FMetasoundFrontendVertex> Inputs;

	// Output vertices to node.
	UPROPERTY()
	TArray<FMetasoundFrontendVertex> Outputs;

	// Environment variables of node.
	UPROPERTY()
	TArray<FMetasoundFrontendVertex> Environment;
};

// DEPRECATED in Document Model v1.1
UENUM()
enum class EMetasoundFrontendNodeStyleDisplayVisibility : uint8
{
	Visible,
	Hidden
};

USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendNodeStyleDisplay
{
	GENERATED_BODY()

	// DEPRECATED in Document Model v1.1: Visibility state of node
	UPROPERTY()
	EMetasoundFrontendNodeStyleDisplayVisibility Visibility = EMetasoundFrontendNodeStyleDisplayVisibility::Visible;

	// Map of visual node guid to 2D location. May have more than one if the node allows displaying in
	// more than one place on the graph (Only functionally relevant for nodes that cannot contain inputs.)
	UPROPERTY()
	TMap<FGuid, FVector2D> Locations;
};

USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendNodeStyle
{
	GENERATED_BODY()

	// Display style of a node
	UPROPERTY()
	FMetasoundFrontendNodeStyleDisplay Display;

	UPROPERTY()
	bool bIsPrivate = false;
};

// An FMetasoundFrontendNode represents a single instance of a FMetasoundFrontendClass
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendNode
{
	GENERATED_BODY()

	FMetasoundFrontendNode() = default;

	// Construct node to satisfy class. 
	FMetasoundFrontendNode(const FMetasoundFrontendClass& InClass);

	// Unique ID of this node.
	UPROPERTY()
	FGuid ID = Metasound::FrontendInvalidID;

	// ID of FMetasoundFrontendClass corresponding to this node.
	UPROPERTY()
	FGuid ClassID = Metasound::FrontendInvalidID;

	// Name of node instance. 
	UPROPERTY()
	FString Name;

	// Interface of node instance.
	UPROPERTY()
	FMetasoundFrontendNodeInterface Interface;

	// Default values for node inputs.
	UPROPERTY()
	TArray<FMetasoundFrontendVertexLiteral> InputLiterals;

	// Style info related to a node.
	UPROPERTY()
	FMetasoundFrontendNodeStyle Style;
};


// Represents a single connection from one point to another.
USTRUCT()
struct FMetasoundFrontendEdge
{
	GENERATED_BODY()

	// ID of source node.
	UPROPERTY()
	FGuid FromNodeID = Metasound::FrontendInvalidID;

	// ID of source point on source node.
	UPROPERTY()
	FGuid FromVertexID = Metasound::FrontendInvalidID;

	// ID of destination node.
	UPROPERTY()
	FGuid ToNodeID = Metasound::FrontendInvalidID;

	// ID of destination point on destination node.
	UPROPERTY()
	FGuid ToVertexID = Metasound::FrontendInvalidID;
};

// Display style for an edge.
UENUM()
enum class EMetasoundFrontendStyleEdgeDisplay : uint8
{
	Default,
	Inherited,
	Hidden
};

// Styling for edges
USTRUCT()
struct FMetasoundFrontendStyleEdge
{
	GENERATED_BODY()

	UPROPERTY()
	EMetasoundFrontendStyleEdgeDisplay Display = EMetasoundFrontendStyleEdgeDisplay::Default;
};

// Styling for a class of edges dependent upon edge data type.
USTRUCT()
struct FMetasoundFrontendStyleEdgeClass
{
	GENERATED_BODY()

	// Datatype of edge to apply style to
	UPROPERTY()
	FName TypeName;

	// Style information for edge.
	UPROPERTY()
	FMetasoundFrontendStyleEdge Style;
};

// Styling for a class
USTRUCT() 
struct FMetasoundFrontendGraphStyle 
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsGraphEditable = true;

	// Edge styles for graph.
	UPROPERTY()
	TArray<FMetasoundFrontendStyleEdgeClass> EdgeStyles;
};

USTRUCT()
struct FMetasoundFrontendGraph
{
	GENERATED_BODY()

	// Node contained in graph
	UPROPERTY()
	TArray<FMetasoundFrontendNode> Nodes;

	// Connections between points on nodes.
	UPROPERTY()
	TArray<FMetasoundFrontendEdge> Edges;

	// Style of graph display.
	UPROPERTY()
	FMetasoundFrontendGraphStyle Style;
};

// Metadata associated with a vertex.
USTRUCT() 
struct FMetasoundFrontendVertexMetadata
{
	GENERATED_BODY()

	// Display name for a vertex
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (DisplayName = "Name"))
	FText DisplayName;

	// Description of the vertex.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FText Description;

	// Keywords associated with the vertex
	UPROPERTY()
	TArray<FString> Keywords;

	// Vertexes of the same group are generally placed together. 
	UPROPERTY()
	FString Group;

	// If true, vertex is shown for advanced display.
	UPROPERTY()
	bool bIsAdvancedDisplay = false;
};

USTRUCT()
struct FMetasoundFrontendEnvironmentVariableMetadata
{
	GENERATED_BODY()

	// Display name for a environment variable
	UPROPERTY()
	FText DisplayName;

	// Description of the environment variable
	UPROPERTY()
	FText Description;
};



USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendClassVertex : public FMetasoundFrontendVertex
{
	GENERATED_BODY()

	virtual ~FMetasoundFrontendClassVertex() = default;

	UPROPERTY()
	FGuid NodeID = Metasound::FrontendInvalidID;

	// Metadata associated with input.
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendVertexMetadata Metadata;

	static bool IsFunctionalEquivalent(const FMetasoundFrontendClassVertex& InLHS, const FMetasoundFrontendClassVertex& InRHS);
};

// Information regarding how to display a node class
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendClassStyleDisplay
{
	GENERATED_BODY()

	FMetasoundFrontendClassStyleDisplay() = default;

	FMetasoundFrontendClassStyleDisplay(const Metasound::FNodeDisplayStyle& InDisplayStyle)
	:	ImageName(InDisplayStyle.ImageName)
	,	bShowName(InDisplayStyle.bShowName)
	,	bShowInputNames(InDisplayStyle.bShowInputNames)
	,	bShowOutputNames(InDisplayStyle.bShowOutputNames)
	{
	}

	UPROPERTY()
	FName ImageName;

	UPROPERTY()
	bool bShowName = true;

	UPROPERTY()
	bool bShowInputNames = true;

	UPROPERTY()
	bool bShowOutputNames = true;
};

// Contains info for input vertex of a Metasound class.
USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendClassInput : public FMetasoundFrontendClassVertex
{
	GENERATED_BODY()

	FMetasoundFrontendClassInput() = default;

	FMetasoundFrontendClassInput(const FMetasoundFrontendClassVertex& InOther);

	virtual ~FMetasoundFrontendClassInput() = default;

	// Default value for this input.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FMetasoundFrontendLiteral DefaultLiteral;
};

// Contains info for variable vertex of a Metasound class.
USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendClassVariable : public FMetasoundFrontendClassVertex
{
	GENERATED_BODY()

	FMetasoundFrontendClassVariable() = default;

	FMetasoundFrontendClassVariable(const FMetasoundFrontendClassVertex& InOther);

	virtual ~FMetasoundFrontendClassVariable() = default;

	// Default value for this variable.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FMetasoundFrontendLiteral DefaultLiteral;
};

// Contains info for output vertex of a Metasound class.
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendClassOutput : public FMetasoundFrontendClassVertex
{
	GENERATED_BODY()

	FMetasoundFrontendClassOutput() = default;

	FMetasoundFrontendClassOutput(const FMetasoundFrontendClassVertex& InOther)
	:	FMetasoundFrontendClassVertex(InOther)
	{
	}

	virtual ~FMetasoundFrontendClassOutput() = default;
};

USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendEnvironmentVariable
{
	GENERATED_BODY()

	virtual ~FMetasoundFrontendEnvironmentVariable() = default;

	// Name of environment variable.
	UPROPERTY()
	FString Name;

	// Type of environment variable.
	UPROPERTY()
	FName TypeName;

	// Metadata of environment variable.
	UPROPERTY()
	FMetasoundFrontendEnvironmentVariableMetadata Metadata;
};

USTRUCT() 
struct METASOUNDFRONTEND_API FMetasoundFrontendClassEnvironmentVariable : public FMetasoundFrontendEnvironmentVariable
{
	GENERATED_BODY()

	virtual ~FMetasoundFrontendClassEnvironmentVariable() = default;

	// True if the environment variable is needed in order to instantiate a node instance of the class. 
	UPROPERTY()
	bool bIsRequired = true;
};

// Layout mode for an interface.
UENUM()
enum class EMetasoundFrontendStyleInterfaceLayoutMode : uint8
{
	Default,
	Inherited
};


// Style info of an interface.
USTRUCT()
struct FMetasoundFrontendInterfaceStyle
{
	GENERATED_BODY()

	// Interface layout mode
	UPROPERTY()
	EMetasoundFrontendStyleInterfaceLayoutMode LayoutMode = EMetasoundFrontendStyleInterfaceLayoutMode::Inherited;

	// Default vertex sort order, where array index mirrors array interface index and value is display sort index.
	UPROPERTY()
	TArray<int32> DefaultSortOrder;

	template <typename HandleType>
	TArray<HandleType> SortDefaults(const TArray<HandleType>& InHandles) const
	{
		TArray<HandleType> SortedHandles = InHandles;

		// TODO: Hack for assets which aren't getting sort order set for inputs/outputs. Fix this & remove size check.
		if (DefaultSortOrder.Num() > 0)
		{
			if (ensure(SortedHandles.Num() == DefaultSortOrder.Num()))
			{
				TMap<FGuid, int32> HandleIDToSortIndex;
				for (int32 i = 0; i < DefaultSortOrder.Num(); ++i)
				{
					if (InHandles.IsValidIndex(i))
					{
						const int32 SortIndex = DefaultSortOrder[i];
						HandleIDToSortIndex.Add(InHandles[i]->GetID(), SortIndex);
					}
				}

				SortedHandles.Sort([&](const HandleType& HandleA, const HandleType& HandleB)
					{
						const FGuid HandleAID = HandleA->GetID();
						const FGuid HandleBID = HandleB->GetID();
						return HandleIDToSortIndex[HandleAID] < HandleIDToSortIndex[HandleBID];
					});
			}
		}

		return SortedHandles;
	}
};


USTRUCT()
struct FMetasoundFrontendClassInterface
{
	GENERATED_BODY()

	// Style info for inputs.
	UPROPERTY()
	FMetasoundFrontendInterfaceStyle InputStyle;

	// Style info for outputs.
	UPROPERTY()
	FMetasoundFrontendInterfaceStyle OutputStyle;

	// Description of class inputs.
	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundFrontendClassInput> Inputs;

	// Description of class outputs.
	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundFrontendClassOutput> Outputs;

	// Description of class environment variables.
	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundFrontendClassEnvironmentVariable> Environment;
};


// Name of a Metasound class
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendClassName
{
	GENERATED_BODY()

	FMetasoundFrontendClassName() = default;

	FMetasoundFrontendClassName(const FName& InNamespace, const FName& InName, const FName& InVariant);

	FMetasoundFrontendClassName(const Metasound::FNodeClassName& InName);

	// Namespace of class.
	UPROPERTY(EditAnywhere, Category = General)
	FName Namespace;

	// Name of class.
	UPROPERTY(EditAnywhere, Category = General)
	FName Name;

	// Variant of class. The Variant is used to describe an equivalent class which performs the same operation but on differing types.
	UPROPERTY(EditAnywhere, Category = General)
	FName Variant;

	// Returns a full name of the class.
	FName GetFullName() const;

	// Returns scoped name representing namespace and name. 
	FName GetScopedName() const;

	// Returns NodeClassName version of full name
	Metasound::FNodeClassName ToNodeClassName() const
	{
		return { Namespace, Name, Variant };
	}

	// Return string version of full name.
	FString ToString() const;

	METASOUNDFRONTEND_API friend bool operator==(const FMetasoundFrontendClassName& InLHS, const FMetasoundFrontendClassName& InRHS);
};


USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendClassMetadata
{
	GENERATED_BODY()

	FMetasoundFrontendClassMetadata() = default;

	FMetasoundFrontendClassMetadata(const Metasound::FNodeClassMetadata& InNodeClassMetadata);

	UPROPERTY(EditAnywhere, Category = Metasound)
	FMetasoundFrontendClassName ClassName;

	UPROPERTY(EditAnywhere, Category = Metasound)
	FMetasoundFrontendVersionNumber Version;

	UPROPERTY(VisibleAnywhere, Category = Metasound)
	EMetasoundFrontendClassType Type = EMetasoundFrontendClassType::Invalid;

	UPROPERTY(EditAnywhere, Category = Metasound)
	FText DisplayName;

	UPROPERTY(EditAnywhere, Category = Metasound)
	FText Description;

	UPROPERTY(EditAnywhere, Category = Metasound)
	FText PromptIfMissing;

	UPROPERTY(EditAnywhere, Category = Metasound)
	FText Author;

	UPROPERTY(EditAnywhere, Category = Metasound)
	TArray<FName> Keywords;

	UPROPERTY(EditAnywhere, Category = Metasound)
	TArray<FText> CategoryHierarchy;
};

USTRUCT()
struct FMetasoundFrontendClassStyle
{
	GENERATED_BODY()

	UPROPERTY()
	FMetasoundFrontendClassStyleDisplay Display;
};

USTRUCT()
struct FMetasoundFrontendEditorData
{
	GENERATED_BODY()

	UPROPERTY()
	FMetasoundFrontendVersion Version;

	UPROPERTY()
	TArray<uint8> Data;
};

USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendClass
{
	GENERATED_BODY()

	virtual ~FMetasoundFrontendClass() = default;

	UPROPERTY()
	FGuid ID = Metasound::FrontendInvalidID;

	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendClassMetadata Metadata;

	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendClassInterface Interface;

	UPROPERTY()
	FMetasoundFrontendEditorData EditorData;

	UPROPERTY()
	FMetasoundFrontendClassStyle Style;
};

USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendGraphClass : public FMetasoundFrontendClass
{
	GENERATED_BODY()

	FMetasoundFrontendGraphClass();

	virtual ~FMetasoundFrontendGraphClass() = default;

	UPROPERTY()
	FMetasoundFrontendGraph Graph;

};

USTRUCT()
struct FMetasoundFrontendDocumentMetadata
{
	GENERATED_BODY()

	UPROPERTY()
	FMetasoundFrontendVersion Version;
};


USTRUCT()
struct FMetasoundFrontendArchetypeInterface
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FMetasoundFrontendClassVertex> Inputs;

	UPROPERTY()
	TArray<FMetasoundFrontendClassVertex> Outputs;

	UPROPERTY()
	TArray<FMetasoundFrontendEnvironmentVariable> Environment;
};

// This is used to describe the required inputs and outputs for a metasound, and is used to make sure we can use a metasound graph for specific applications.
// For example, a UMetaSoundSource needs to generate audio, so its RequiredOutputs will contain "MainAudioOutput"
USTRUCT()
struct FMetasoundFrontendArchetype
{
	GENERATED_BODY()

	// Name of the archetype we're using.
	UPROPERTY()
	FName Name;

	UPROPERTY()
	FMetasoundFrontendVersionNumber Version;

	UPROPERTY()
	FMetasoundFrontendArchetypeInterface Interface;
};


USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendDocument
{
	GENERATED_BODY()

	//Metasound::Frontend::TAccessPoint<FMetasoundFrontendDocument> AccessPoint;
	Metasound::Frontend::FAccessPoint AccessPoint;

	FMetasoundFrontendDocument();

	UPROPERTY(EditAnywhere, Category = Metadata)
	FMetasoundFrontendDocumentMetadata Metadata;

	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendGraphClass RootGraph;

	UPROPERTY()
	TArray<FMetasoundFrontendGraphClass> Subgraphs;

	UPROPERTY()
	FMetasoundFrontendEditorData EditorData;

	UPROPERTY()
	TArray<FMetasoundFrontendClass> Dependencies;
};


