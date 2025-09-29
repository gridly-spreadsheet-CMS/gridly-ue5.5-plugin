// Copyright (c) 2021 LocalizeDirect AB

#include "GridlySourceStringModifier.h"
#include "GridlyEditor.h"
#include "GridlyGameSettings.h"
#include "GridlyLocalizedText.h"
#include "LocTextHelper.h"
#include "LocalizationConfigurationScript.h"
#include "LocalizationTargetTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFilemanager.h"

DEFINE_LOG_CATEGORY(LogGridlySourceStringModifier);

bool FGridlySourceStringModifier::AnalyzeSourceStringChanges(
    ULocalizationTarget* LocalizationTarget,
    const TArray<FPolyglotTextData>& IncomingData,
    TArray<FSourceStringChange>& OutChanges)
{
    if (!LocalizationTarget)
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("LocalizationTarget is null"));
        return false;
    }

    // Load existing manifest data using FLocTextHelper
    TArray<FPolyglotTextData> ExistingPolyglotTextDatas;
    TSharedPtr<FLocTextHelper> LocTextHelper;
    
    if (!FGridlyLocalizedText::GetAllTextAsPolyglotTextDatas(LocalizationTarget, ExistingPolyglotTextDatas, LocTextHelper))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to load existing manifest data for target: %s"), *LocalizationTarget->Settings.Name);
        return false;
    }

    // Create a map of existing entries for quick lookup
    TMap<FString, const FPolyglotTextData*> ExistingEntries;
    for (const FPolyglotTextData& ExistingData : ExistingPolyglotTextDatas)
    {
        FString UniqueKey = FString::Printf(TEXT("%s,%s"), *ExistingData.GetNamespace(), *ExistingData.GetKey());
        ExistingEntries.Add(UniqueKey, &ExistingData);
    }

    // Compare incoming data with existing data
    for (const FPolyglotTextData& IncomingEntry : IncomingData)
    {
        FString UniqueKey = FString::Printf(TEXT("%s,%s"), *IncomingEntry.GetNamespace(), *IncomingEntry.GetKey());
        const FPolyglotTextData** ExistingEntryPtr = ExistingEntries.Find(UniqueKey);

        if (ExistingEntryPtr && *ExistingEntryPtr)
        {
            // Entry exists, check if source text has changed
            const FPolyglotTextData* ExistingEntry = *ExistingEntryPtr;
            if (ExistingEntry->GetNativeString() != IncomingEntry.GetNativeString())
            {
                FSourceStringChange Change;
                Change.Namespace = IncomingEntry.GetNamespace();
                Change.Key = IncomingEntry.GetKey();
                Change.OldSourceText = ExistingEntry->GetNativeString();
                Change.NewSourceText = IncomingEntry.GetNativeString();
                Change.bIsNewEntry = false;

                // Try to get source location from the helper
                if (LocTextHelper.IsValid())
                {
                    TSharedPtr<FManifestEntry> ManifestEntry = LocTextHelper->FindSourceText(Change.Namespace, Change.Key);
                    if (ManifestEntry.IsValid())
                    {
                        const FManifestContext* Context = ManifestEntry->FindContextByKey(Change.Key);
                        if (Context)
                        {
                            Change.SourceLocation = Context->SourceLocation;
                        }
                    }
                }

                OutChanges.Add(Change);
                UE_LOG(LogGridlySourceStringModifier, Log, TEXT("Detected source string change for %s,%s: '%s' -> '%s'"), 
                    *Change.Namespace, *Change.Key, *Change.OldSourceText, *Change.NewSourceText);
            }
        }
        else
        {
            // New entry
            FSourceStringChange Change;
            Change.Namespace = IncomingEntry.GetNamespace();
            Change.Key = IncomingEntry.GetKey();
            Change.OldSourceText = TEXT("");
            Change.NewSourceText = IncomingEntry.GetNativeString();
            Change.bIsNewEntry = true;
            Change.SourceLocation = TEXT("Gridly Import");

            OutChanges.Add(Change);
            UE_LOG(LogGridlySourceStringModifier, Log, TEXT("Detected new source string entry: %s,%s = '%s'"), 
                *Change.Namespace, *Change.Key, *Change.NewSourceText);
        }
    }

    return OutChanges.Num() > 0;
}

bool FGridlySourceStringModifier::BackupManifestFile(
    ULocalizationTarget* LocalizationTarget,
    FString& OutBackupPath)
{
    if (!LocalizationTarget)
    {
        return false;
    }

    const FString ManifestPath = GetManifestFilePath(LocalizationTarget);
    if (ManifestPath.IsEmpty() || !FPaths::FileExists(ManifestPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Manifest file not found: %s"), *ManifestPath);
        return false;
    }

    // Create backup with timestamp
    const FString TimeStamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString BackupDir = FPaths::GetPath(ManifestPath) / TEXT("Backups");
    const FString BackupFileName = FString::Printf(TEXT("%s_backup_%s.manifest"), 
        *FPaths::GetBaseFilename(ManifestPath), *TimeStamp);
    OutBackupPath = BackupDir / BackupFileName;

    // Ensure backup directory exists
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*BackupDir))
    {
        if (!PlatformFile.CreateDirectoryTree(*BackupDir))
        {
            UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to create backup directory: %s"), *BackupDir);
            return false;
        }
    }

    // Copy manifest file to backup location
    if (!PlatformFile.CopyFile(*OutBackupPath, *ManifestPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to create backup: %s -> %s"), *ManifestPath, *OutBackupPath);
        return false;
    }

    UE_LOG(LogGridlySourceStringModifier, Log, TEXT("Created manifest backup: %s"), *OutBackupPath);
    return true;
}

bool FGridlySourceStringModifier::ApplySourceStringModifications(
    ULocalizationTarget* LocalizationTarget,
    const TArray<FSourceStringChange>& Changes,
    FModificationResult& Result)
{
    Result = FModificationResult();

    if (!LocalizationTarget || Changes.Num() == 0)
    {
        Result.bSuccess = true; // No changes to apply
        return true;
    }

    const FString ManifestPath = GetManifestFilePath(LocalizationTarget);
    if (ManifestPath.IsEmpty())
    {
        Result.ErrorMessage = TEXT("Could not determine manifest file path");
        return false;
    }



    // Load manifest as JSON
    TSharedPtr<FJsonObject> ManifestJson;
    if (!LoadManifestAsJson(ManifestPath, ManifestJson))
    {
        Result.ErrorMessage = TEXT("Failed to load manifest file as JSON");
        return false;
    }

    // Apply changes
    for (const FSourceStringChange& Change : Changes)
    {
        if (UpdateManifestEntry(ManifestJson, Change))
        {
            if (Change.bIsNewEntry)
            {
                Result.NewEntries.Add(Change);
            }
            else
            {
                Result.ModifiedEntries.Add(Change);
            }
        }
        else
        {
            UE_LOG(LogGridlySourceStringModifier, Warning, TEXT("Failed to update manifest entry: %s,%s"), 
                *Change.Namespace, *Change.Key);
        }
    }

    // Save modified manifest
    if (!SaveJsonToManifest(ManifestPath, ManifestJson))
    {
        Result.ErrorMessage = TEXT("Failed to save modified manifest file");
        return false;
    }

    Result.bSuccess = true;
    UE_LOG(LogGridlySourceStringModifier, Log, TEXT("Successfully applied %d source string modifications (%d new, %d modified)"), 
        Changes.Num(), Result.NewEntries.Num(), Result.ModifiedEntries.Num());

    return true;
}

bool FGridlySourceStringModifier::RestoreFromBackup(
    ULocalizationTarget* LocalizationTarget,
    const FString& BackupPath)
{
    if (!LocalizationTarget || BackupPath.IsEmpty())
    {
        return false;
    }

    const FString ManifestPath = GetManifestFilePath(LocalizationTarget);
    if (ManifestPath.IsEmpty())
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Could not determine manifest file path"));
        return false;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.FileExists(*BackupPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Backup file not found: %s"), *BackupPath);
        return false;
    }

    if (!PlatformFile.CopyFile(*ManifestPath, *BackupPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to restore from backup: %s -> %s"), *BackupPath, *ManifestPath);
        return false;
    }

    UE_LOG(LogGridlySourceStringModifier, Log, TEXT("Successfully restored manifest from backup: %s"), *BackupPath);
    return true;
}

FString FGridlySourceStringModifier::GetManifestFilePath(ULocalizationTarget* LocalizationTarget)
{
    if (!LocalizationTarget)
    {
        return FString();
    }

    const FString ConfigFilePath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocalizationTarget);
    const FString SectionName = TEXT("CommonSettings");

    FString SourcePath;
    if (!GConfig->GetString(*SectionName, TEXT("SourcePath"), SourcePath, ConfigFilePath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("No source path specified in config"));
        return FString();
    }

    FString ManifestName;
    if (!GConfig->GetString(*SectionName, TEXT("ManifestName"), ManifestName, ConfigFilePath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("No manifest name specified in config"));
        return FString();
    }

    // Determine if this is an engine or project manifest
    const FString ConfigFullPath = FPaths::ConvertRelativePathToFull(ConfigFilePath);
    const FString EngineFullPath = FPaths::ConvertRelativePathToFull(FPaths::EngineConfigDir());
    const bool bIsEngineManifest = ConfigFullPath.StartsWith(EngineFullPath);

    FString ManifestPath;
    if (bIsEngineManifest)
    {
        ManifestPath = FPaths::Combine(*FPaths::EngineDir(), *SourcePath, *ManifestName);
    }
    else
    {
        ManifestPath = FPaths::Combine(*FPaths::ProjectDir(), *SourcePath, *ManifestName);
    }

    return FPaths::ConvertRelativePathToFull(ManifestPath);
}

bool FGridlySourceStringModifier::LoadManifestAsJson(const FString& ManifestPath, TSharedPtr<FJsonObject>& OutJson)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to load manifest file: %s"), *ManifestPath);
        return false;
    }

    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(JsonReader, OutJson) || !OutJson.IsValid())
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to parse manifest JSON: %s"), *ManifestPath);
        return false;
    }

    return true;
}

bool FGridlySourceStringModifier::SaveJsonToManifest(const FString& ManifestPath, const TSharedPtr<FJsonObject>& JsonObject)
{
    FString OutputString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to serialize manifest JSON"));
        return false;
    }

    if (!FFileHelper::SaveStringToFile(OutputString, *ManifestPath))
    {
        UE_LOG(LogGridlySourceStringModifier, Error, TEXT("Failed to save manifest file: %s"), *ManifestPath);
        return false;
    }

    return true;
}

TSharedPtr<FJsonObject> FGridlySourceStringModifier::FindManifestEntry(
    const TSharedPtr<FJsonObject>& ManifestJson,
    const FString& Namespace,
    const FString& Key)
{
    if (!ManifestJson.IsValid())
    {
        return nullptr;
    }

    const TArray<TSharedPtr<FJsonValue>>* Children;
    if (!ManifestJson->TryGetArrayField(TEXT("Children"), Children))
    {
        return nullptr;
    }

    for (const TSharedPtr<FJsonValue>& Child : *Children)
    {
        const TSharedPtr<FJsonObject> ChildObj = Child->AsObject();
        if (!ChildObj.IsValid())
        {
            continue;
        }

        FString ChildNamespace;
        if (!ChildObj->TryGetStringField(TEXT("Namespace"), ChildNamespace) || ChildNamespace != Namespace)
        {
            continue;
        }

        const TArray<TSharedPtr<FJsonValue>>* ChildrenArray;
        if (!ChildObj->TryGetArrayField(TEXT("Children"), ChildrenArray))
        {
            continue;
        }

        for (const TSharedPtr<FJsonValue>& GrandChild : *ChildrenArray)
        {
            const TSharedPtr<FJsonObject> GrandChildObj = GrandChild->AsObject();
            if (!GrandChildObj.IsValid())
            {
                continue;
            }

            FString ChildKey;
            if (GrandChildObj->TryGetStringField(TEXT("Key"), ChildKey) && ChildKey == Key)
            {
                return GrandChildObj;
            }
        }
    }

    return nullptr;
}

bool FGridlySourceStringModifier::UpdateManifestEntry(
    const TSharedPtr<FJsonObject>& ManifestJson,
    const FSourceStringChange& Change)
{
    if (!ManifestJson.IsValid())
    {
        return false;
    }

    // Find existing entry
    TSharedPtr<FJsonObject> ExistingEntry = FindManifestEntry(ManifestJson, Change.Namespace, Change.Key);

    if (ExistingEntry.IsValid())
    {
        // Update existing entry
        const TSharedPtr<FJsonObject> Source = ExistingEntry->GetObjectField(TEXT("Source"));
        if (Source.IsValid())
        {
            Source->SetStringField(TEXT("Text"), Change.NewSourceText);
            UE_LOG(LogGridlySourceStringModifier, Verbose, TEXT("Updated existing manifest entry: %s,%s"), 
                *Change.Namespace, *Change.Key);
            return true;
        }
    }
    else if (Change.bIsNewEntry)
    {
        // Create new entry - this is more complex and requires proper manifest structure
        // For now, we'll log this and return false to indicate it needs manual handling
        UE_LOG(LogGridlySourceStringModifier, Warning, TEXT("Creating new manifest entries is not yet supported: %s,%s"), 
            *Change.Namespace, *Change.Key);
        return false;
    }

    return false;
}