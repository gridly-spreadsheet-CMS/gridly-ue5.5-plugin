// Copyright (c) 2021 LocalizeDirect AB

#pragma once

#include "CoreMinimal.h"

#include "ILocalizationServiceOperation.h"
#include "ILocalizationServiceProvider.h"
#include "ILocalizationServiceState.h"
#include "Interfaces/IHttpRequest.h"
#include <string>
#include <fstream>
#include <iostream>


class FGridlyLocalizationServiceProvider final : public ILocalizationServiceProvider
{


	class FGridlyTypeRecord
	{
	public:
		FString Id;
		FString Path;

		FGridlyTypeRecord(const FString& InId, const FString& InPath)
			: Id(InId), Path(InPath)
		{}
	};
public:
	FGridlyLocalizationServiceProvider();
	bool bHasDeletesPending = false;
	bool HasDeleteRequestsPending() const;

public:
	/* ILocalizationServiceProvider implementation */

	virtual void Init(bool bForceConnection = true) override;
	virtual void Close() override;
	virtual FText GetStatusText() const override;
	virtual bool IsEnabled() const override;
	virtual bool IsAvailable() const override;
	virtual const FName& GetName(void) const override;
	virtual const FText GetDisplayName() const override;
	virtual ELocalizationServiceOperationCommandResult::Type GetState(
		const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
		TArray<TSharedRef<ILocalizationServiceState, ESPMode::ThreadSafe>>& OutState,
		ELocalizationServiceCacheUsage::Type InStateCacheUsage) override;
	virtual ELocalizationServiceOperationCommandResult::Type Execute(
		const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation,
		const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
		ELocalizationServiceOperationConcurrency::Type InConcurrency = ELocalizationServiceOperationConcurrency::Synchronous,
		const FLocalizationServiceOperationComplete& InOperationCompleteDelegate =
		FLocalizationServiceOperationComplete()) override;
	virtual bool CanCancelOperation(
		const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation) const override;
	virtual void CancelOperation(const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation) override;
	virtual void Tick() override;

#if LOCALIZATION_SERVICES_WITH_SLATE
	virtual void CustomizeSettingsDetails(IDetailCategoryBuilder& DetailCategoryBuilder) const override;
	virtual void CustomizeTargetDetails(
		IDetailCategoryBuilder& DetailCategoryBuilder, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const override;
	virtual void CustomizeTargetToolbar(
		TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const override;
	virtual void CustomizeTargetSetToolbar(
		TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTargetSet> LocalizationTargetSet) const override;
	void AddTargetToolbarButtons(FToolBarBuilder& ToolbarBuilder, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget,
		TSharedRef<FUICommandList> CommandList);
#endif	  // LOCALIZATION_SERVICES_WITH_SLATE

	// functions to run export/import from commandlet
	FHttpRequestCompleteDelegate CreateExportNativeCultureDelegate();
	bool HasRequestsPending() const;

	void ExportForTargetToGridly(ULocalizationTarget* LocalizationTarget, FHttpRequestCompleteDelegate& ReqDelegate, const FText& SlowTaskText, bool bIncTargetTranslation = false);

	// New functions for fetching and parsing CSV from Gridly
	void FetchGridlyCSV(); // Fetches the CSV data from Gridly
	void OnGridlyCSVResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful); // Callback for when the CSV is received
	void ParseCSVAndCreateRecords(const FString& CSVContent); // Parses CSV content and creates records

private:
	// Import
	bool IsFileNotEmpty(const std::string& filePath);
	void ImportAllCulturesForTargetFromGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet);
	void OnImportCultureForTargetFromGridly(const FLocalizationServiceOperationRef& Operation,
		ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet);
	TSharedPtr<FScopedSlowTask> ImportAllCulturesForTargetFromGridlySlowTask;
	TArray<FString> CurrentCultureDownloads;
	int SuccessfulDownloads;
	size_t ExportForTargetEntriesDeleted = 0;


	// Export

	size_t ExportForTargetEntriesUpdated;
	TSharedPtr<FScopedSlowTask> ExportForTargetToGridlySlowTask;
	TQueue<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> ExportFromTargetRequestQueue;
	bool bExportRequestInProgress = false;

	void ExportNativeCultureForTargetToGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet);
	void OnExportNativeCultureForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess);

	// Export all

	void ExportTranslationsForTargetToGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet);
	void OnExportTranslationsForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess);

	TArray<FGridlyTypeRecord> GridlyRecords; // List to store the records from Gridly
	TArray<FGridlyTypeRecord> UERecords;
	FString RemoveNamespaceFromKey(FString& InputString);
	
	void DeleteRecordsFromGridly(const TArray<FString>& RecordsToDelete);
	void OnDeleteRecordsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	int32 CompletedBatches;         // Track the number of completed batches
	int32 TotalBatchesToProcess;    // Track the total number of batches
};
