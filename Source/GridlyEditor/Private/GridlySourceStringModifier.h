// Copyright (c) 2021 LocalizeDirect AB

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/PolyglotTextData.h"

class FLocTextHelper;
class ULocalizationTarget;

DECLARE_LOG_CATEGORY_EXTERN(LogGridlySourceStringModifier, Log, All);

/**
 * Utility class for safely modifying source strings in UE localization manifests
 * WARNING: This functionality goes against UE's standard "author-at-source" localization workflow
 */
class GRIDLYEDITOR_API FGridlySourceStringModifier
{
public:
    struct FSourceStringChange
    {
        FString Namespace;
        FString Key;
        FString OldSourceText;
        FString NewSourceText;
        FString SourceLocation;
        bool bIsNewEntry = false;
    };

    struct FModificationResult
    {
        bool bSuccess = false;
        TArray<FSourceStringChange> ModifiedEntries;
        TArray<FSourceStringChange> NewEntries;
        FString ErrorMessage;
        FString BackupFilePath;
    };

public:
    /**
     * Analyzes incoming polyglot text data and compares it with existing manifest entries
     * @param LocalizationTarget The target to analyze
     * @param IncomingData The polyglot text data from CSV import
     * @param OutChanges Array of detected source string changes
     * @return True if any source string changes were detected
     */
    static bool AnalyzeSourceStringChanges(
        ULocalizationTarget* LocalizationTarget,
        const TArray<FPolyglotTextData>& IncomingData,
        TArray<FSourceStringChange>& OutChanges
    );

    /**
     * Creates a backup of the manifest file before modification
     * @param LocalizationTarget The target whose manifest to backup
     * @param OutBackupPath The path where the backup was created
     * @return True if backup was successful
     */
    static bool BackupManifestFile(
        ULocalizationTarget* LocalizationTarget,
        FString& OutBackupPath
    );

    /**
     * Applies source string modifications to the manifest file
     * @param LocalizationTarget The target to modify
     * @param Changes The changes to apply
     * @param Result Output result structure with success/failure information
     * @return True if modifications were successful
     */
    static bool ApplySourceStringModifications(
        ULocalizationTarget* LocalizationTarget,
        const TArray<FSourceStringChange>& Changes,
        FModificationResult& Result
    );

    /**
     * Restores a manifest file from backup
     * @param LocalizationTarget The target to restore
     * @param BackupPath The path to the backup file
     * @return True if restore was successful
     */
    static bool RestoreFromBackup(
        ULocalizationTarget* LocalizationTarget,
        const FString& BackupPath
    );

private:
    /**
     * Gets the manifest file path for a localization target
     */
    static FString GetManifestFilePath(ULocalizationTarget* LocalizationTarget);

    /**
     * Loads manifest content as JSON
     */
    static bool LoadManifestAsJson(const FString& ManifestPath, TSharedPtr<FJsonObject>& OutJson);

    /**
     * Saves JSON content to manifest file
     */
    static bool SaveJsonToManifest(const FString& ManifestPath, const TSharedPtr<FJsonObject>& JsonObject);

    /**
     * Finds a manifest entry in JSON by namespace and key
     */
    static TSharedPtr<FJsonObject> FindManifestEntry(
        const TSharedPtr<FJsonObject>& ManifestJson,
        const FString& Namespace,
        const FString& Key
    );

    /**
     * Updates or creates a manifest entry in JSON
     */
    static bool UpdateManifestEntry(
        const TSharedPtr<FJsonObject>& ManifestJson,
        const FSourceStringChange& Change
    );
};