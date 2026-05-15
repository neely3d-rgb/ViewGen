// Copyright ViewGen. All Rights Reserved.

#include "ComfyNodeDatabase.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FComfyNodeDatabase& FComfyNodeDatabase::Get()
{
	static FComfyNodeDatabase Instance;
	return Instance;
}

/**
 * Strip emoji and other non-BMP Unicode characters that Slate cannot render.
 * Also trims any leading/trailing whitespace left behind after removal.
 */
static FString StripEmoji(const FString& Input)
{
	FString Result;
	Result.Reserve(Input.Len());

	const TCHAR* Ptr = *Input;
	while (*Ptr)
	{
		// UTF-16 surrogate pair (emoji / non-BMP) — skip both code units
		if (*Ptr >= 0xD800 && *Ptr <= 0xDBFF)
		{
			Ptr++; // high surrogate
			if (*Ptr >= 0xDC00 && *Ptr <= 0xDFFF)
			{
				Ptr++; // low surrogate
			}
			continue;
		}

		// Skip common emoji block characters in the BMP (misc symbols, dingbats,
		// variation selectors, zero-width joiners, etc.)
		TCHAR Ch = *Ptr;
		bool bSkip =
			(Ch >= 0x2600 && Ch <= 0x27BF) ||  // Misc Symbols + Dingbats
			(Ch >= 0x2B50 && Ch <= 0x2B55) ||  // Additional symbols
			(Ch >= 0xFE00 && Ch <= 0xFE0F) ||  // Variation Selectors
			(Ch == 0x200D) ||                   // Zero-Width Joiner
			(Ch == 0x20E3);                     // Combining Enclosing Keycap

		if (!bSkip)
		{
			Result.AppendChar(Ch);
		}
		Ptr++;
	}

	Result.TrimStartAndEndInline();
	return Result;
}

void FComfyNodeDatabase::ParseObjectInfo(TSharedPtr<FJsonObject> Root)
{
	if (!Root.IsValid()) return;

	NodeDefs.Empty();

	for (const auto& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* NodeInfoPtr;
		if (!Pair.Value->TryGetObject(NodeInfoPtr) || !(*NodeInfoPtr).IsValid())
		{
			continue;
		}

		FComfyNodeDef Def = ParseSingleNode(Pair.Key, *NodeInfoPtr);

		// Diagnostic: log all inputs for nodes containing "ByteDance" or "Seedance"
		if (Pair.Key.Contains(TEXT("ByteDance")) || Pair.Key.Contains(TEXT("Seedance")) ||
			Def.DisplayName.Contains(TEXT("ByteDance")) || Def.DisplayName.Contains(TEXT("Seedance")))
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen: [V3 Diag] Node '%s' (%s) has %d inputs:"),
				*Pair.Key, *Def.DisplayName, Def.Inputs.Num());
			for (const auto& Input : Def.Inputs)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: [V3 Diag]   '%s' type='%s' isLink=%d combo=%d"),
					*Input.Name, *Input.Type, Input.IsLinkType() ? 1 : 0, Input.ComboOptions.Num());
			}
		}

		NodeDefs.Add(Pair.Key, MoveTemp(Def));
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen: ComfyNodeDatabase populated with %d node types"), NodeDefs.Num());

	OnDatabaseRefreshed.Broadcast();
}

FComfyNodeDef FComfyNodeDatabase::ParseSingleNode(const FString& ClassType, TSharedPtr<FJsonObject> NodeInfo) const
{
	FComfyNodeDef Def;
	Def.ClassType = ClassType;

	// Display name — strip emoji that Slate cannot render
	if (!NodeInfo->TryGetStringField(TEXT("display_name"), Def.DisplayName) || Def.DisplayName.IsEmpty())
	{
		Def.DisplayName = ClassType;
	}
	Def.DisplayName = StripEmoji(Def.DisplayName);
	if (Def.DisplayName.IsEmpty())
	{
		Def.DisplayName = ClassType;
	}

	// Category — also strip emoji from category paths
	NodeInfo->TryGetStringField(TEXT("category"), Def.Category);
	Def.Category = StripEmoji(Def.Category);

	// Description
	NodeInfo->TryGetStringField(TEXT("description"), Def.Description);

	// Output node flag
	NodeInfo->TryGetBoolField(TEXT("output_node"), Def.bIsOutputNode);

	// ---- Parse Inputs ----
	const TSharedPtr<FJsonObject>* InputObj;
	if (NodeInfo->TryGetObjectField(TEXT("input"), InputObj))
	{
		auto ParseInputGroup = [&Def, &ClassType](const TSharedPtr<FJsonObject>& GroupObj, bool bRequired)
		{
			if (!GroupObj.IsValid()) return;

			for (const auto& InputPair : GroupObj->Values)
			{
				// Skip if this input was already added (e.g. by V3 nested parsing with
				// a more specific type like IMAGE instead of top-level STRING)
				bool bAlreadyExists = false;
				for (const auto& Existing : Def.Inputs)
				{
					if (Existing.Name == InputPair.Key) { bAlreadyExists = true; break; }
				}
				if (bAlreadyExists) continue;

				FComfyInputDef InputDef;
				InputDef.Name = InputPair.Key;
				InputDef.bRequired = bRequired;

				const TArray<TSharedPtr<FJsonValue>>* InputArray;
				if (!InputPair.Value->TryGetArray(InputArray) || InputArray->Num() == 0)
				{
					continue;
				}

				// First element determines the type
				FString TypeStr;
				if ((*InputArray)[0]->TryGetString(TypeStr))
				{
					InputDef.Type = TypeStr;

					// If type is "COMBO" or a V3 dynamic combo, options are in element [1]
					// New format: ["COMBO", {"options": [...]}]
					// V3 format: ["COMFY_DYNAMICCOMBO_V3", {"options": [...], "default": "..."}]
					bool bIsComboLike = (TypeStr == TEXT("COMBO") || TypeStr.StartsWith(TEXT("COMFY_DYNAMICCOMBO")));
					if (bIsComboLike && InputArray->Num() >= 2)
					{
						// Normalize V3 dynamic combo to COMBO for consistent widget handling
						if (TypeStr != TEXT("COMBO"))
						{
							InputDef.Type = TEXT("COMBO");
						}

						// Check if second element is an object with "options" key
						const TSharedPtr<FJsonObject>* MetaObj;
						const TArray<TSharedPtr<FJsonValue>>* DirectOptionsArray;
						if ((*InputArray)[1]->TryGetObject(MetaObj))
						{
							const TArray<TSharedPtr<FJsonValue>>* Options;
							if ((*MetaObj)->TryGetArrayField(TEXT("options"), Options))
							{
								for (const auto& Opt : *Options)
								{
									FString OptStr;
									if (Opt->TryGetString(OptStr))
									{
										InputDef.ComboOptions.Add(OptStr);
									}
									else
									{
										// V3 dynamic combo: options are objects with "key" and optional nested "inputs"
										// e.g. {"key": "Normal", "inputs": {"required": {"pbr": ["BOOLEAN", {"default": false}]}}}
										const TSharedPtr<FJsonObject>* OptObj;
										if (Opt->TryGetObject(OptObj))
										{
											FString Key;
											if ((*OptObj)->TryGetStringField(TEXT("key"), Key))
											{
												InputDef.ComboOptions.Add(Key);
											}

											// Parse nested conditional inputs — these become additional inputs
											// on the node with dot-notation keys: "model.prompt", "model.reference_images"
											// V3 nodes may put link-type inputs (IMAGE, VIDEO, AUDIO) under "optional"
											const TSharedPtr<FJsonObject>* NestedInputs;
											if ((*OptObj)->TryGetObjectField(TEXT("inputs"), NestedInputs))
											{
												// Helper: parse one group of nested inputs (required or optional)
												auto ParseNestedGroup = [&Def, &InputPair, &ClassType](const TSharedPtr<FJsonObject>& GroupObj)
												{
													if (!GroupObj.IsValid()) return;

													for (const auto& SubPair : GroupObj->Values)
													{
														// Build a dot-notation key: "parentName.subName"
														FString SubKey = InputPair.Key + TEXT(".") + SubPair.Key;

														// Only add if we haven't seen this sub-input yet
														bool bAlreadyExists = false;
														for (const auto& Existing : Def.Inputs)
														{
															if (Existing.Name == SubKey) { bAlreadyExists = true; break; }
														}
														if (bAlreadyExists) continue;

														// Parse the sub-input definition (same format as regular inputs)
														const TArray<TSharedPtr<FJsonValue>>* SubArray;
														if (!SubPair.Value->TryGetArray(SubArray) || SubArray->Num() == 0)
															continue;

														FComfyInputDef SubDef;
														SubDef.Name = SubKey;
														SubDef.bRequired = false; // Conditional on parent combo value

														FString SubType;
														if ((*SubArray)[0]->TryGetString(SubType))
														{
															SubDef.Type = SubType;

															// Handle nested COMBO options
															bool bSubCombo = (SubType == TEXT("COMBO") || SubType.StartsWith(TEXT("COMFY_DYNAMICCOMBO")));
															if (bSubCombo)
															{
																SubDef.Type = TEXT("COMBO");
																if (SubArray->Num() >= 2)
																{
																	const TSharedPtr<FJsonObject>* SubMeta;
																	if ((*SubArray)[1]->TryGetObject(SubMeta))
																	{
																		const TArray<TSharedPtr<FJsonValue>>* SubOpts;
																		if ((*SubMeta)->TryGetArrayField(TEXT("options"), SubOpts))
																		{
																			for (const auto& SO : *SubOpts)
																			{
																				FString SOStr;
																				if (SO->TryGetString(SOStr))
																					SubDef.ComboOptions.Add(SOStr);
																			}
																		}
																	}
																}
															}

															// Parse defaults from second element
															if (SubArray->Num() >= 2)
															{
																const TSharedPtr<FJsonObject>* SubConstraint;
																if ((*SubArray)[1]->TryGetObject(SubConstraint))
																{
																	double SubVal;
																	if ((*SubConstraint)->TryGetNumberField(TEXT("default"), SubVal))
																		SubDef.DefaultNumber = SubVal;
																	FString SubStrVal;
																	if ((*SubConstraint)->TryGetStringField(TEXT("default"), SubStrVal))
																		SubDef.DefaultString = SubStrVal;
																	bool SubBoolVal;
																	if ((*SubConstraint)->TryGetBoolField(TEXT("default"), SubBoolVal))
																		SubDef.DefaultBool = SubBoolVal;
																}
															}
														}

														Def.Inputs.Add(MoveTemp(SubDef));
													}
												};

												// Parse both required AND optional nested inputs
												const TSharedPtr<FJsonObject>* NestedReq;
												if ((*NestedInputs)->TryGetObjectField(TEXT("required"), NestedReq))
												{
													ParseNestedGroup(*NestedReq);
												}
												const TSharedPtr<FJsonObject>* NestedOpt;
												if ((*NestedInputs)->TryGetObjectField(TEXT("optional"), NestedOpt))
												{
													ParseNestedGroup(*NestedOpt);
												}
											}
										}
									}
								}
							}

							// V3 types may carry a "default" string
							FString DefaultStr;
							if ((*MetaObj)->TryGetStringField(TEXT("default"), DefaultStr))
							{
								InputDef.DefaultString = DefaultStr;
							}

							if (InputDef.ComboOptions.Num() == 0)
							{
								UE_LOG(LogTemp, Warning, TEXT("ViewGen: Node '%s' input '%s' (type=%s) has meta object but 0 combo options"),
									*ClassType, *InputPair.Key, *TypeStr);
							}
						}
						else if ((*InputArray)[1]->TryGetArray(DirectOptionsArray))
						{
							// Alternative format: second element is a direct array of options
							// e.g. ["COMBO", ["option1", "option2", ...]]
							for (const auto& Opt : *DirectOptionsArray)
							{
								FString OptStr;
								if (Opt->TryGetString(OptStr))
								{
									InputDef.ComboOptions.Add(OptStr);
								}
							}
							UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' input '%s' parsed %d options from direct array format"),
								*ClassType, *InputPair.Key, InputDef.ComboOptions.Num());
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("ViewGen: Node '%s' input '%s' (type=%s) — second element is neither object nor array"),
								*ClassType, *InputPair.Key, *TypeStr);
						}
					}
					else if (bIsComboLike)
					{
						// V3 dynamic combo with only 1 element — no meta/options at all
						InputDef.Type = TEXT("COMBO");
						UE_LOG(LogTemp, Warning, TEXT("ViewGen: Node '%s' input '%s' (type=%s) has no second element for options"),
							*ClassType, *InputPair.Key, *TypeStr);
					}
				}
				else
				{
					// Old format: first element is an array of options (COMBO)
					const TArray<TSharedPtr<FJsonValue>>* OptionsArray;
					if ((*InputArray)[0]->TryGetArray(OptionsArray))
					{
						InputDef.Type = TEXT("COMBO");
						for (const auto& Opt : *OptionsArray)
						{
							FString OptStr;
							if (Opt->TryGetString(OptStr))
							{
								InputDef.ComboOptions.Add(OptStr);
							}
						}
					}
				}

				// Parse constraints from second element (if object)
				if (InputArray->Num() >= 2)
				{
					const TSharedPtr<FJsonObject>* ConstraintObj;
					if ((*InputArray)[1]->TryGetObject(ConstraintObj))
					{
						double Val;
						if ((*ConstraintObj)->TryGetNumberField(TEXT("default"), Val))
						{
							InputDef.DefaultNumber = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("min"), Val))
						{
							InputDef.MinValue = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("max"), Val))
						{
							InputDef.MaxValue = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("step"), Val))
						{
							InputDef.Step = Val;
						}

						FString StrVal;
						if ((*ConstraintObj)->TryGetStringField(TEXT("default"), StrVal))
						{
							InputDef.DefaultString = StrVal;
						}
					}
				}

				Def.Inputs.Add(MoveTemp(InputDef));
			}
		};

		const TSharedPtr<FJsonObject>* RequiredObj;
		if ((*InputObj)->TryGetObjectField(TEXT("required"), RequiredObj))
		{
			ParseInputGroup(*RequiredObj, true);
		}

		const TSharedPtr<FJsonObject>* OptionalObj;
		if ((*InputObj)->TryGetObjectField(TEXT("optional"), OptionalObj))
		{
			ParseInputGroup(*OptionalObj, false);
		}
	}

	// ---- Parse Outputs ----
	const TArray<TSharedPtr<FJsonValue>>* OutputNames;
	if (NodeInfo->TryGetArrayField(TEXT("output"), OutputNames))
	{
		const TArray<TSharedPtr<FJsonValue>>* OutputNameDisplays;
		NodeInfo->TryGetArrayField(TEXT("output_name"), OutputNameDisplays);

		for (int32 i = 0; i < OutputNames->Num(); ++i)
		{
			FComfyOutputDef OutDef;
			(*OutputNames)[i]->TryGetString(OutDef.Type);

			if (OutputNameDisplays && i < OutputNameDisplays->Num())
			{
				(*OutputNameDisplays)[i]->TryGetString(OutDef.Name);
			}

			if (OutDef.Name.IsEmpty())
			{
				OutDef.Name = OutDef.Type;
			}

			Def.Outputs.Add(MoveTemp(OutDef));
		}
	}

	return Def;
}

const FComfyNodeDef* FComfyNodeDatabase::FindNode(const FString& ClassType) const
{
	return NodeDefs.Find(ClassType);
}

TArray<FString> FComfyNodeDatabase::GetCategories() const
{
	TSet<FString> CategorySet;
	for (const auto& Pair : NodeDefs)
	{
		if (!Pair.Value.Category.IsEmpty())
		{
			CategorySet.Add(Pair.Value.Category);
		}
	}

	TArray<FString> Result = CategorySet.Array();
	Result.Sort();
	return Result;
}

TArray<const FComfyNodeDef*> FComfyNodeDatabase::GetNodesInCategory(const FString& Category) const
{
	TArray<const FComfyNodeDef*> Result;
	for (const auto& Pair : NodeDefs)
	{
		if (Pair.Value.Category == Category)
		{
			Result.Add(&Pair.Value);
		}
	}

	Result.Sort([](const FComfyNodeDef& A, const FComfyNodeDef& B)
	{
		return A.DisplayName < B.DisplayName;
	});

	return Result;
}

TArray<const FComfyNodeDef*> FComfyNodeDatabase::SearchNodes(const FString& Query) const
{
	TArray<const FComfyNodeDef*> Result;
	FString LowerQuery = Query.ToLower();

	for (const auto& Pair : NodeDefs)
	{
		if (Pair.Value.DisplayName.ToLower().Contains(LowerQuery) ||
			Pair.Value.ClassType.ToLower().Contains(LowerQuery) ||
			Pair.Value.Category.ToLower().Contains(LowerQuery))
		{
			Result.Add(&Pair.Value);
		}
	}

	// Sort by relevance: exact class_type match first, then display name match
	Result.Sort([&LowerQuery](const FComfyNodeDef& A, const FComfyNodeDef& B)
	{
		bool AExact = A.ClassType.ToLower() == LowerQuery;
		bool BExact = B.ClassType.ToLower() == LowerQuery;
		if (AExact != BExact) return AExact;
		return A.DisplayName < B.DisplayName;
	});

	return Result;
}
