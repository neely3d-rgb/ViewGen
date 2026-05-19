// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Describes a single input on a ComfyUI node type.
 * Populated from the /object_info endpoint.
 */
struct FComfyInputDef
{
	FString Name;

	/** The data type expected: "MODEL", "CLIP", "CONDITIONING", "LATENT", "IMAGE", "VAE",
	 *  "CONTROL_NET", "INT", "FLOAT", "STRING", "COMBO", "BOOLEAN", etc. */
	FString Type;

	/** Whether this is a required input (true) or optional (false) */
	bool bRequired = true;

	/** For COMBO inputs: the list of selectable values */
	TArray<FString> ComboOptions;

	/** For numeric inputs: default, min, max, step */
	double DefaultNumber = 0.0;
	double MinValue = 0.0;
	double MaxValue = 1.0;
	double Step = 0.01;

	/** For STRING inputs: default value */
	FString DefaultString;

	/** For BOOLEAN inputs: default value */
	bool DefaultBool = false;

	/** Returns true if this is a link/connection type (not a widget value) */
	bool IsLinkType() const
	{
		// These are the known value types that appear as widgets, not connections
		static const TSet<FString> WidgetTypes = {
			TEXT("INT"), TEXT("FLOAT"), TEXT("STRING"), TEXT("COMBO"),
			TEXT("BOOLEAN"), TEXT("BOOL")
		};
		if (WidgetTypes.Contains(Type)) return false;

		// ComfyUI V3 API dynamic combo/widget types (e.g. COMFY_DYNAMICCOMBO_V3)
		// These are widget inputs that should not be treated as connections
		if (Type.StartsWith(TEXT("COMFY_"))) return false;

		return true;
	}
};

/**
 * Describes a single output on a ComfyUI node type.
 */
struct FComfyOutputDef
{
	FString Name;  // e.g. "MODEL", "CLIP", "IMAGE"
	FString Type;  // same as Name in most cases
};

/**
 * Full definition of a ComfyUI node class, parsed from /object_info.
 */
struct FComfyNodeDef
{
	/** The class_type string used in workflow JSON (e.g. "CheckpointLoaderSimple") */
	FString ClassType;

	/** Human-readable display name */
	FString DisplayName;

	/** Category path for menu organization (e.g. "loaders/checkpoints") */
	FString Category;

	/** Description text */
	FString Description;

	/** Ordered list of input definitions (required first, then optional) */
	TArray<FComfyInputDef> Inputs;

	/** Ordered list of output definitions */
	TArray<FComfyOutputDef> Outputs;

	/** Whether this is an output-only node (e.g. SaveImage, PreviewImage) */
	bool bIsOutputNode = false;

	/** Get all link-type inputs (these become input pins) */
	TArray<const FComfyInputDef*> GetLinkInputs() const
	{
		TArray<const FComfyInputDef*> Result;
		for (const auto& Input : Inputs)
		{
			if (Input.IsLinkType())
			{
				Result.Add(&Input);
			}
		}
		return Result;
	}

	/** Get all widget inputs (these become editable fields on the node body) */
	TArray<const FComfyInputDef*> GetWidgetInputs() const
	{
		TArray<const FComfyInputDef*> Result;
		for (const auto& Input : Inputs)
		{
			if (!Input.IsLinkType())
			{
				Result.Add(&Input);
			}
		}
		return Result;
	}
};

/**
 * Singleton database of all ComfyUI node definitions.
 * Populated asynchronously from /object_info.
 */
class FComfyNodeDatabase
{
public:
	static FComfyNodeDatabase& Get();

	/** Parse the full /object_info JSON response and populate the database */
	void ParseObjectInfo(TSharedPtr<FJsonObject> Root);

	/** Check if the database has been populated */
	bool IsPopulated() const { return NodeDefs.Num() > 0; }

	/** Get a node definition by class_type. Returns nullptr if not found. */
	const FComfyNodeDef* FindNode(const FString& ClassType) const;

	/** Get all node definitions */
	const TMap<FString, FComfyNodeDef>& GetAllNodes() const { return NodeDefs; }

	/** Get all unique categories */
	TArray<FString> GetCategories() const;

	/** Get all node defs in a given category */
	TArray<const FComfyNodeDef*> GetNodesInCategory(const FString& Category) const;

	/** Search nodes by name (case-insensitive substring match) */
	TArray<const FComfyNodeDef*> SearchNodes(const FString& Query) const;

	/**
	 * Scan workflow JSON files in the given directory and synthesize node definitions
	 * for any node types not already present in the database.
	 * This picks up frontend-only API nodes (e.g. Anthropic Claude) that do not appear
	 * in /object_info because they lack backend Python registrations.
	 */
	void InjectNodesFromWorkflows(const FString& WorkflowDir);

	/** Delegate: fired when the database is refreshed */
	DECLARE_MULTICAST_DELEGATE(FOnDatabaseRefreshed);
	FOnDatabaseRefreshed OnDatabaseRefreshed;

private:
	FComfyNodeDef ParseSingleNode(const FString& ClassType, TSharedPtr<FJsonObject> NodeInfo) const;

	TMap<FString, FComfyNodeDef> NodeDefs;
};
