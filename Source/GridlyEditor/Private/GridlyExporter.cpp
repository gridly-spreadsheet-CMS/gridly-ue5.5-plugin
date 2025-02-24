#include "GridlyExporter.h"

#include "GridlyCultureConverter.h"
#include "GridlyDataTableImporterJSON.h"
#include "GridlyGameSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/PolyglotTextData.h"
#include "LocTextHelper.h"

bool FGridlyExporter::ConvertToJson(const TArray<FPolyglotTextData>& PolyglotTextDatas,
	bool bIncludeTargetTranslations, const TSharedPtr<FLocTextHelper>& LocTextHelperPtr, FString& OutJsonString)
{
	UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const TArray<FString> TargetCultures = FGridlyCultureConverter::GetTargetCultures();

	const bool bUseCombinedNamespaceKey = GameSettings->bUseCombinedNamespaceId;
	const bool bExportNamespace = !bUseCombinedNamespaceKey || GameSettings->bAlsoExportNamespaceColumn;
	const bool bUsePathAsNamespace = GameSettings->NamespaceColumnId == "path";

	TArray<TSharedPtr<FJsonValue>> Rows;

	for (int i = 0; i < PolyglotTextDatas.Num(); i++)
	{
		TSharedPtr<FJsonObject> RowJsonObject = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> CellsJsonArray;

		const FString& Key = PolyglotTextDatas[i].GetKey();
		const FString& Namespace = PolyglotTextDatas[i].GetNamespace();

		const FManifestContext* ItemContext = nullptr;
		if (LocTextHelperPtr.IsValid())
		{
			TSharedPtr<FManifestEntry> ManifestEntry = LocTextHelperPtr->FindSourceText(Namespace, Key);
			ItemContext = ManifestEntry ? ManifestEntry->FindContextByKey(Key) : nullptr;
		}

		// Set record id

		if (bUseCombinedNamespaceKey)
		{
			// Use Contains method to check for the substring "blueprints/"
			if (Namespace.Contains(TEXT("blueprints/"))) {
				RowJsonObject->SetStringField("id", FString::Printf(TEXT("%s,%s"), TEXT(""), *Key));
			}
			else {
				RowJsonObject->SetStringField("id", FString::Printf(TEXT("%s,%s"), *Namespace, *Key));
			}
		}

		else
		{
			RowJsonObject->SetStringField("id", Key);
		}

		// Set namespace/path

		if (bExportNamespace)
		{
			if (bUsePathAsNamespace)
			{
				RowJsonObject->SetStringField("path", Namespace);
			}
			else if (!GameSettings->NamespaceColumnId.IsEmpty())
			{
				TSharedPtr<FJsonObject> CellJsonObject = MakeShareable(new FJsonObject);
				CellJsonObject->SetStringField("columnId", GameSettings->NamespaceColumnId);
				CellJsonObject->SetStringField("value", Namespace);
				CellsJsonArray.Add(MakeShareable(new FJsonValueObject(CellJsonObject)));
			}
		}

		// Set source language text

		{
			const FString NativeCulture = PolyglotTextDatas[i].GetNativeCulture();
			const FString NativeString = PolyglotTextDatas[i].GetNativeString();

			FString GridlyCulture;
			if (FGridlyCultureConverter::ConvertToGridly(NativeCulture, GridlyCulture))
			{
				TSharedPtr<FJsonObject> CellJsonObject = MakeShareable(new FJsonObject);
				CellJsonObject->SetStringField("columnId", GameSettings->SourceLanguageColumnIdPrefix + GridlyCulture);
				CellJsonObject->SetStringField("value", NativeString);
				CellsJsonArray.Add(MakeShareable(new FJsonValueObject(CellJsonObject)));
			}

			// Add context

			if (ItemContext && GameSettings->bExportContext)
			{				
				TSharedPtr<FJsonObject> CellJsonObject = MakeShareable(new FJsonObject);
				CellJsonObject->SetStringField("columnId", *GameSettings->ContextColumnId);
				CellJsonObject->SetStringField("value",
					ItemContext->SourceLocation.Replace(TEXT(" - line "), TEXT(":"), ESearchCase::CaseSensitive));
				CellsJsonArray.Add(MakeShareable(new FJsonValueObject(CellJsonObject)));
			}

			// Add metadata

 			if (ItemContext && GameSettings->bExportMetadata && ItemContext->InfoMetadataObj.IsValid())
			{
				for (const auto& InfoMetaDataPair : ItemContext->InfoMetadataObj->Values)
				{
					const FString& KeyName = InfoMetaDataPair.Key;
					if (const FGridlyColumnInfo* GridlyColumnInfo = GameSettings->MetadataMapping.Find(InfoMetaDataPair.Key))
					{
						TSharedPtr<FJsonObject> CellJsonObject = MakeShareable(new FJsonObject);
						CellJsonObject->SetStringField("columnId", *GridlyColumnInfo->Name);

						const TSharedPtr<FLocMetadataValue> Value = InfoMetaDataPair.Value;

						switch (GridlyColumnInfo->DataType)
						{
							case EGridlyColumnDataType::String:
							{
								CellJsonObject->SetStringField("value", Value->ToString());
							}
							break;
							case EGridlyColumnDataType::Number:
							{
								CellJsonObject->SetNumberField("value", FCString::Atoi(*Value->ToString()));
							}
							break;
							default:
								break;
						}

						CellsJsonArray.Add(MakeShareable(new FJsonValueObject(CellJsonObject)));
					}
				}
			}

			if (bIncludeTargetTranslations)
			{
				for (int j = 0; j < TargetCultures.Num(); j++)
				{
					const FString CultureName = TargetCultures[j];
					FString LocalizedString;

					if (CultureName != NativeCulture
					    && PolyglotTextDatas[i].GetLocalizedString(CultureName, LocalizedString)
					    && FGridlyCultureConverter::ConvertToGridly(CultureName, GridlyCulture))
					{
						TSharedPtr<FJsonObject> CellJsonObject = MakeShareable(new FJsonObject);
						CellJsonObject->SetStringField("columnId", GameSettings->TargetLanguageColumnIdPrefix + GridlyCulture);
						CellJsonObject->SetStringField("value", LocalizedString);
						CellsJsonArray.Add(MakeShareable(new FJsonValueObject(CellJsonObject)));
					}
				}
			}
		}

		// Assign array

		RowJsonObject->SetArrayField("cells", CellsJsonArray);

		Rows.Add(MakeShareable(new FJsonValueObject(RowJsonObject)));
	}

	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonStringWriter<>::Create(&OutJsonString);
	if (FJsonSerializer::Serialize(Rows, JsonWriter))
	{
		return true;
	}

	return false;
}

bool FGridlyExporter::ConvertToJson(const UGridlyDataTable* GridlyDataTable, FString& OutJsonString, size_t StartIndex,
	size_t MaxSize)
{
	UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const TArray<FString> TargetCultures = FGridlyCultureConverter::GetTargetCultures();

	if (!GridlyDataTable->RowStruct)
	{
		return false;
	}

	FString KeyField = GridlyDataTableJSONUtils::GetKeyFieldName(*GridlyDataTable);

	auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);

	JsonWriter->WriteArrayStart();

	TArray<FName> Keys;
	const TMap<FName, uint8*>& RowMap = GridlyDataTable->GetRowMap();
	RowMap.GenerateKeyArray(Keys);

	if (StartIndex < Keys.Num())
	{
		const size_t EndIndex = FMath::Min(StartIndex + MaxSize, static_cast<size_t>(Keys.Num()));
		for (size_t i = StartIndex; i < EndIndex; i++)
		{
			const FName RowName = Keys[i];
			uint8* RowData = RowMap[RowName];

			JsonWriter->WriteObjectStart();
			{
				// RowName
				JsonWriter->WriteValue("id", RowName.ToString());

				// Now handle the _path field
				FString PathValue;
				bool bPathFound = false;

				// Now the values
				JsonWriter->WriteArrayStart("cells");

				for (TFieldIterator<const FProperty> It(GridlyDataTable->GetRowStruct()); It; ++It)
				{
					const FProperty* BaseProp = *It;
					check(BaseProp);

					const EDataTableExportFlags DTExportFlags = EDataTableExportFlags::None;

					const FString Identifier = DataTableUtils::GetPropertyExportName(BaseProp, DTExportFlags);
					const void* Data = BaseProp->ContainerPtrToValuePtr<void>(RowData, 0);

					// Check if the property is _path and handle it separately
					if (Identifier == "_path")
					{
						PathValue = DataTableUtils::GetPropertyValueAsString(BaseProp, static_cast<uint8*>(RowData), DTExportFlags);
						bPathFound = true;
						continue;  // Skip adding _path to the cells array
					}

					if (BaseProp->ArrayDim == 1)
					{
						JsonWriter->WriteObjectStart();

						const FString ExportId = DataTableUtils::GetPropertyExportName(BaseProp, DTExportFlags);
						JsonWriter->WriteValue("columnId", ExportId);

						if (const FEnumProperty* EnumProp = CastField<const FEnumProperty>(BaseProp))
						{
							const FString PropertyValue = DataTableUtils::GetPropertyValueAsString(EnumProp,
								static_cast<uint8*>(RowData), DTExportFlags);
							JsonWriter->WriteValue("value", PropertyValue);
						}
						else if (const FNumericProperty* NumProp = CastField<const FNumericProperty>(BaseProp))
						{
							if (NumProp->IsEnum())
							{
								const FString PropertyValue = DataTableUtils::GetPropertyValueAsString(BaseProp,
									static_cast<uint8*>(RowData), DTExportFlags);
								JsonWriter->WriteValue("value", PropertyValue);
							}
							else if (NumProp->IsInteger())
							{
								const int64 PropertyValue = NumProp->GetSignedIntPropertyValue(Data);
								JsonWriter->WriteValue("value", PropertyValue);
							}
							else
							{
								const double PropertyValue = NumProp->GetFloatingPointPropertyValue(Data);
								JsonWriter->WriteValue("value", PropertyValue);
							}
						}
						else if (const FBoolProperty* BoolProp = CastField<const FBoolProperty>(BaseProp))
						{
							const bool PropertyValue = BoolProp->GetPropertyValue(Data);
							JsonWriter->WriteValue("value", PropertyValue);
						}
						else
						{
							const FString PropertyValue = DataTableUtils::GetPropertyValueAsString(BaseProp,
								static_cast<uint8*>(RowData), DTExportFlags);
							JsonWriter->WriteValue("value", PropertyValue);
						}

						JsonWriter->WriteObjectEnd();
					}
				}

				JsonWriter->WriteArrayEnd();
				
				// Now add the path if it was found
				if (bPathFound)
				{
					JsonWriter->WriteValue("path", *PathValue);  // Explicitly convert FString to TCHAR*
				}
				else
				{
					JsonWriter->WriteValue("path", TEXT(""));  // Correctly pass an empty TCHAR* string
				}

				
			}
			JsonWriter->WriteObjectEnd();
		}

		JsonWriter->WriteArrayEnd();

		if (JsonWriter->Close())
		{
			return true;
		}
	}

	return false;
}

