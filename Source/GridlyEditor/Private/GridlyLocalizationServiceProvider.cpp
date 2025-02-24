// Copyright (c) 2021 LocalizeDirect AB

#include "GridlyLocalizationServiceProvider.h"

#include "GridlyEditor.h"
#include "GridlyExporter.h"
#include "GridlyGameSettings.h"
#include "GridlyLocalizedText.h"
#include "GridlyLocalizedTextConverter.h"
#include "GridlyStyle.h"
#include "GridlyTask_DownloadLocalizedTexts.h"
#include "HttpModule.h"
#include "ILocalizationServiceModule.h"
#include "LocalizationCommandletTasks.h"
#include "LocalizationModule.h"
#include "LocalizationTargetTypes.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IMainFrameModule.h"
#include "Internationalization/Culture.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include <filesystem>




#if LOCALIZATION_SERVICES_WITH_SLATE
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#endif

#define LOCTEXT_NAMESPACE "Gridly"

static FName ProviderName("Gridly");

#include "Styling/AppStyle.h" // Ensure this header is included

class FGridlyLocalizationTargetEditorCommands final : public TCommands<FGridlyLocalizationTargetEditorCommands>
{
public:
	FGridlyLocalizationTargetEditorCommands() :
		TCommands<FGridlyLocalizationTargetEditorCommands>("GridlyLocalizationTargetEditor",
			NSLOCTEXT("Gridly", "GridlyLocalizationTargetEditor", "Gridly Localization Target Editor"), NAME_None,
			FAppStyle::GetAppStyleSetName()) // Replace FEditorStyle with FAppStyle
	{
	}

	TSharedPtr<FUICommandInfo> ImportAllCulturesForTargetFromGridly;
	TSharedPtr<FUICommandInfo> ExportNativeCultureForTargetToGridly;
	TSharedPtr<FUICommandInfo> ExportTranslationsForTargetToGridly;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};


void FGridlyLocalizationTargetEditorCommands::RegisterCommands()
{
	UI_COMMAND(ImportAllCulturesForTargetFromGridly, "Import from Gridly",
		"Imports translations for all cultures of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportNativeCultureForTargetToGridly, "Export to Gridly",
		"Exports native culture and source text of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportTranslationsForTargetToGridly, "Export All to Gridly",
		"Exports source text and all translations of this target to Gridly.", EUserInterfaceActionType::Button, FInputChord());
}

FGridlyLocalizationServiceProvider::FGridlyLocalizationServiceProvider()
{
}

void FGridlyLocalizationServiceProvider::Init(bool bForceConnection)
{
	FGridlyLocalizationTargetEditorCommands::Register();
}

void FGridlyLocalizationServiceProvider::Close()
{
}

FText FGridlyLocalizationServiceProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Status"), LOCTEXT("Unknown", "Unknown / not implemented"));

	return FText::Format(LOCTEXT("GridlyStatusText", "Gridly status: {Status}"), Args);
}

bool FGridlyLocalizationServiceProvider::IsEnabled() const
{
	return true;
}

bool FGridlyLocalizationServiceProvider::IsAvailable() const
{
	return true; // Check for server availability
}

const FName& FGridlyLocalizationServiceProvider::GetName(void) const
{
	return ProviderName;
}

const FText FGridlyLocalizationServiceProvider::GetDisplayName() const
{
	return LOCTEXT("GridlyLocalizationService", "Gridly Localization Service");
}

ELocalizationServiceOperationCommandResult::Type FGridlyLocalizationServiceProvider::GetState(
	const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
	TArray<TSharedRef<ILocalizationServiceState, ESPMode::ThreadSafe>>& OutState,
	ELocalizationServiceCacheUsage::Type InStateCacheUsage)
{
	return ELocalizationServiceOperationCommandResult::Succeeded;
}

DEFINE_LOG_CATEGORY_STATIC(LogGridlyLocalizationServiceProvider, Log, All);

ELocalizationServiceOperationCommandResult::Type FGridlyLocalizationServiceProvider::Execute(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation,
	const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds,
	ELocalizationServiceOperationConcurrency::Type InConcurrency /*= ELocalizationServiceOperationConcurrency::Synchronous*/,
	const FLocalizationServiceOperationComplete& InOperationCompleteDelegate /*= FLocalizationServiceOperationComplete()*/)
{
	const TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadOperation =
		StaticCastSharedRef<FDownloadLocalizationTargetFile>(InOperation);
	const FString TargetCulture = DownloadOperation->GetInLocale();

	UGridlyTask_DownloadLocalizedTexts* Task = UGridlyTask_DownloadLocalizedTexts::DownloadLocalizedTexts(nullptr);

	// On success
	Task->OnSuccessDelegate.BindLambda(
		[this, DownloadOperation, InOperationCompleteDelegate, TargetCulture](const TArray<FPolyglotTextData>& PolyglotTextDatas)
		{
			/*
			if (PolyglotTextDatas.Num() > 0)
			{
			*/
			const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectDir() / DownloadOperation->GetInRelativeOutputFilePathAndName());

			bool writeProc = FGridlyLocalizedTextConverter::WritePoFile(PolyglotTextDatas, TargetCulture, AbsoluteFilePathAndName);
			// Callback for successful write
			InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Succeeded);
			/*
			}
			else
			{
				// Handle parse failure
				DownloadOperation->SetOutErrorText(LOCTEXT("GridlyErrorParse", "Failed to parse downloaded content"));
				InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Failed);
			}
			*/
		});

	// On fail
	Task->OnFailDelegate.BindLambda(
		[DownloadOperation, InOperationCompleteDelegate](const TArray<FPolyglotTextData>& PolyglotTextDatas, const FGridlyResult& Error)
		{
			// Handle download failure
			DownloadOperation->SetOutErrorText(FText::FromString(Error.Message));
			InOperationCompleteDelegate.Execute(DownloadOperation, ELocalizationServiceOperationCommandResult::Failed);
		});

	// Activate the task
	Task->Activate();

	return ELocalizationServiceOperationCommandResult::Succeeded;
}



bool FGridlyLocalizationServiceProvider::CanCancelOperation(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation) const
{
	return false;
}

void FGridlyLocalizationServiceProvider::CancelOperation(
	const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation)
{
}

void FGridlyLocalizationServiceProvider::Tick()
{
}

#if LOCALIZATION_SERVICES_WITH_SLATE
void FGridlyLocalizationServiceProvider::CustomizeSettingsDetails(IDetailCategoryBuilder& DetailCategoryBuilder) const
{
	const FText GridlySettingsInfoText = LOCTEXT("GridlySettingsInfo", "Use Project Settings to configure Gridly");
	FDetailWidgetRow& PublicKeyRow = DetailCategoryBuilder.AddCustomRow(GridlySettingsInfoText);
	PublicKeyRow.ValueContent()[SNew(STextBlock).Text(GridlySettingsInfoText)];
	PublicKeyRow.ValueContent().HAlign(EHorizontalAlignment::HAlign_Fill);
}

void FGridlyLocalizationServiceProvider::CustomizeTargetDetails(
	IDetailCategoryBuilder& DetailCategoryBuilder, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const
{
	// Not implemented
}

void FGridlyLocalizationServiceProvider::CustomizeTargetToolbar(
	TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTarget> LocalizationTarget) const
{
	const TSharedRef<FUICommandList> CommandList = MakeShareable(new FUICommandList());

	MenuExtender->AddToolBarExtension("LocalizationService", EExtensionHook::First, CommandList,
		FToolBarExtensionDelegate::CreateRaw(const_cast<FGridlyLocalizationServiceProvider*>(this),
			&FGridlyLocalizationServiceProvider::AddTargetToolbarButtons, LocalizationTarget, CommandList));
}

void FGridlyLocalizationServiceProvider::CustomizeTargetSetToolbar(
	TSharedRef<FExtender>& MenuExtender, TWeakObjectPtr<ULocalizationTargetSet> LocalizationTargetSet) const
{
	// Not implemented
}

void FGridlyLocalizationServiceProvider::AddTargetToolbarButtons(FToolBarBuilder& ToolbarBuilder,
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, TSharedRef<FUICommandList> CommandList)
{
	// Don't add toolbar buttons if target is engine

	if (!LocalizationTarget->IsMemberOfEngineTargetSet())
	{
		const bool bIsTargetSet = false;
		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ImportAllCulturesForTargetFromGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ImportAllCulturesForTargetFromGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(FGridlyLocalizationTargetEditorCommands::Get().ImportAllCulturesForTargetFromGridly,
			NAME_None,
			TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FGridlyStyle::GetStyleSetName(), "Gridly.ImportAction"));

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ExportNativeCultureForTargetToGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ExportNativeCultureForTargetToGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().ExportNativeCultureForTargetToGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ExportAction"));

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().ExportTranslationsForTargetToGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::ExportTranslationsForTargetToGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().ExportTranslationsForTargetToGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ExportAllAction"));
	}
}
#endif	  // LOCALIZATION_SERVICES_WITH_SLATE

void FGridlyLocalizationServiceProvider::ImportAllCulturesForTargetFromGridly(
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"All local translations to non-native languages will be overwritten. Are you sure you wish to update?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		TArray<FString> Cultures;

		for (int i = 0; i < LocalizationTarget->Settings.SupportedCulturesStatistics.Num(); i++)
		{
			
			if (i != LocalizationTarget->Settings.NativeCultureIndex)
			{
				const FCultureStatistics CultureStats = LocalizationTarget->Settings.SupportedCulturesStatistics[i];
				Cultures.Add(CultureStats.CultureName);
			}
		}

		CurrentCultureDownloads.Append(Cultures);
		SuccessfulDownloads = 0;

		const float AmountOfWork = CurrentCultureDownloads.Num();
		ImportAllCulturesForTargetFromGridlySlowTask = MakeShareable(new FScopedSlowTask(AmountOfWork,
			LOCTEXT("ImportAllCulturesForTargetFromGridlyText", "Importing all cultures for target from Gridly")));

		ImportAllCulturesForTargetFromGridlySlowTask->MakeDialog();

		for (const FString& CultureName : Cultures)
		{
			ILocalizationServiceProvider& Provider = ILocalizationServiceModule::Get().GetProvider();
			TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadTargetFileOp =
				ILocalizationServiceOperation::Create<FDownloadLocalizationTargetFile>();
			DownloadTargetFileOp->SetInTargetGuid(LocalizationTarget->Settings.Guid);
			DownloadTargetFileOp->SetInLocale(CultureName);

			FString Path = FPaths::ProjectSavedDir() / "Temp" / "Game" / LocalizationTarget->Settings.Name / CultureName /
				LocalizationTarget->Settings.Name + ".po";
			FPaths::MakePathRelativeTo(Path, *FPaths::ProjectDir());
			DownloadTargetFileOp->SetInRelativeOutputFilePathAndName(Path);

			// Check the file length and delete if it is empty
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (PlatformFile.FileExists(*Path))
			{
				int64 FileSize = PlatformFile.FileSize(*Path);
				if (FileSize <= 0)
				{
					PlatformFile.DeleteFile(*Path);
					UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("Deleted empty file: %s"), *Path);
					continue;
				}
			}

			auto OperationCompleteDelegate = FLocalizationServiceOperationComplete::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly, bIsTargetSet);

			Provider.Execute(DownloadTargetFileOp, TArray<FLocalizationServiceTranslationIdentifier>(),
				ELocalizationServiceOperationConcurrency::Synchronous, OperationCompleteDelegate);

			ImportAllCulturesForTargetFromGridlySlowTask->EnterProgressFrame(1.f);
		}

		ImportAllCulturesForTargetFromGridlySlowTask.Reset();
	}
}





void FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly(const FLocalizationServiceOperationRef& Operation,
	ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet)
{
	TSharedPtr<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadLocalizationTargetOp = StaticCastSharedRef<
		FDownloadLocalizationTargetFile>(Operation);

	CurrentCultureDownloads.Remove(DownloadLocalizationTargetOp->GetInLocale());

	if (Result == ELocalizationServiceOperationCommandResult::Succeeded)
	{
		SuccessfulDownloads++;
	}
	else
	{
		const FText ErrorMessage = DownloadLocalizationTargetOp->GetOutErrorText();
		UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorMessage.ToString());
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage.ToString()));
	}

	if (CurrentCultureDownloads.Num() == 0 && SuccessfulDownloads > 0)
	{
		const FString TargetName = FPaths::GetBaseFilename(DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

		const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);
		const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

		UE_LOG(LogGridlyEditor, Log, TEXT("Loading from file: %s"), *AbsoluteFilePathAndName);

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();

		if (!bIsTargetSet)
		{

			//here we call the gather
			LocalizationCommandletTasks::ImportTextForTarget(MainFrameParentWindow.ToSharedRef(), Target,
				FPaths::GetPath(FPaths::GetPath(AbsoluteFilePathAndName)));

			Target->UpdateWordCountsFromCSV();
			Target->UpdateStatusFromConflictReport();



		}
	}
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateExportRequest(const TArray<FPolyglotTextData>& PolyglotTextDatas,
	const TSharedPtr<FLocTextHelper>& LocTextHelperPtr, bool bIncludeTargetTranslations)
{
	FString JsonString;
	FGridlyExporter::ConvertToJson(PolyglotTextDatas, bIncludeTargetTranslations, LocTextHelperPtr, JsonString);
	UE_LOG(LogGridlyEditor, Log, TEXT("Creating export request with %d entries"), PolyglotTextDatas.Num());

	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FString ApiKey = GameSettings->ExportApiKey;
	const FString ViewId = GameSettings->ExportViewId;

	FStringFormatNamedArguments Args;
	Args.Add(TEXT("ViewId"), *ViewId);
	const FString Url = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/records"), Args);

	auto HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetContentAsString(JsonString);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(Url);

	return HttpRequest;
}

void FGridlyLocalizationServiceProvider::ExportNativeCultureForTargetToGridly(
	TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"This will overwrite your source strings on Gridly with the data in your UE54 project. Are you sure you wish to export?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		ULocalizationTarget* InLocalizationTarget = LocalizationTarget.Get();
		if (InLocalizationTarget)
		{
			FHttpRequestCompleteDelegate ReqDelegate = FHttpRequestCompleteDelegate::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly);

			const FText SlowTaskText = LOCTEXT("ExportNativeCultureForTargetToGridlyText",
				"Exporting native culture for target to Gridly");

			ExportForTargetToGridly(InLocalizationTarget, ReqDelegate, SlowTaskText);
		}
	}
}

void FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess)
{
	UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();

	const bool bSyncRecords = GameSettings->bSyncRecords;
	if (bSuccess)
	{
		if (HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Ok || HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Created)
		{
			// Success: process the response and log the result
			const FString Content = HttpResponsePtr->GetContentAsString();
			const auto JsonStringReader = TJsonReaderFactory<TCHAR>::Create(Content);
			TArray<TSharedPtr<FJsonValue>> JsonValueArray;
			FJsonSerializer::Deserialize(JsonStringReader, JsonValueArray);
			ExportForTargetEntriesUpdated += JsonValueArray.Num();

			// Continue processing or log success...

			// Check if more requests are pending
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> NextRequest;
			if (ExportFromTargetRequestQueue.Dequeue(NextRequest))
			{
				NextRequest->ProcessRequest();
			}
			else
			{
				// Call FetchGridlyCSV here after all export operations are done
				if (bSyncRecords) {
					FetchGridlyCSV();
				}

				if (!IsRunningCommandlet())
				{
					FString Message = FString::Printf(TEXT("Number of entries updated: %llu"),
						ExportForTargetEntriesUpdated);  // Include deleted records

					UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
					ExportForTargetToGridlySlowTask.Reset();
				}

				bExportRequestInProgress = false;
				
			}
		}
		else
		{
			// Handle HTTP error
			const FString Content = HttpResponsePtr->GetContentAsString();
			const FString ErrorReason = FString::Printf(TEXT("Error: %d, reason: %s"), HttpResponsePtr->GetResponseCode(), *Content);
			UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorReason);

			if (!IsRunningCommandlet())
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorReason));
				ExportForTargetToGridlySlowTask.Reset();
			}

			bExportRequestInProgress = false;
		}
	}
	else
	{
		// Handle failure
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GridlyConnectionError", "ERROR: Unable to connect to Gridly"));
			ExportForTargetToGridlySlowTask.Reset();
		}

		bExportRequestInProgress = false;
	}
	
}


void FGridlyLocalizationServiceProvider::ExportTranslationsForTargetToGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget,
	bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());
	UERecords.Empty();
	GridlyRecords.Empty();

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmText",
			"This will overwrite all your source strings AND translations on Gridly with the data in your UE54 project. Are you sure you wish to export?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		ULocalizationTarget* InLocalizationTarget = LocalizationTarget.Get();
		if (InLocalizationTarget)
		{
			FHttpRequestCompleteDelegate ReqDelegate = FHttpRequestCompleteDelegate::CreateRaw(this,
				&FGridlyLocalizationServiceProvider::OnExportTranslationsForTargetToGridly);

			const FText SlowTaskText = LOCTEXT("ExportTranslationsForTargetToGridlyText",
				"Exporting source text and translations for target to Gridly");

			ExportForTargetToGridly(InLocalizationTarget, ReqDelegate, SlowTaskText, true);
		}
	}
}

void FGridlyLocalizationServiceProvider::OnExportTranslationsForTargetToGridly(FHttpRequestPtr HttpRequestPtr, FHttpResponsePtr HttpResponsePtr, bool bSuccess)
{
	if (bSuccess)
	{
		if (HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Ok || HttpResponsePtr->GetResponseCode() == EHttpResponseCodes::Created)
		{
			// Success: process the response
			const FString Content = HttpResponsePtr->GetContentAsString();
			const auto JsonStringReader = TJsonReaderFactory<TCHAR>::Create(Content);
			TArray<TSharedPtr<FJsonValue>> JsonValueArray;
			FJsonSerializer::Deserialize(JsonStringReader, JsonValueArray);
			ExportForTargetEntriesUpdated += JsonValueArray.Num();

			// Continue processing or log success...

			// Check if more requests are pending
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> NextRequest;
			if (ExportFromTargetRequestQueue.Dequeue(NextRequest))
			{
				NextRequest->ProcessRequest();
			}
			else
			{
				// All export operations completed
				const FString Message = FString::Printf(TEXT("Number of entries updated: %llu"), ExportForTargetEntriesUpdated);
				UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);

				if (!IsRunningCommandlet())
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
					ExportForTargetToGridlySlowTask.Reset();
				}

				bExportRequestInProgress = false;

				// Call FetchGridlyCSV here after all export operations are done
				FetchGridlyCSV();
			}
		}
		else
		{
			// Handle HTTP error
			const FString Content = HttpResponsePtr->GetContentAsString();
			const FString ErrorReason = FString::Printf(TEXT("Error: %d, reason: %s"), HttpResponsePtr->GetResponseCode(), *Content);
			UE_LOG(LogGridlyEditor, Error, TEXT("%s"), *ErrorReason);

			if (!IsRunningCommandlet())
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorReason));
				ExportForTargetToGridlySlowTask.Reset();
			}

			bExportRequestInProgress = false;
		}
	}
	else
	{
		// Handle failure
		if (!IsRunningCommandlet())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GridlyConnectionError", "ERROR: Unable to connect to Gridly"));
			ExportForTargetToGridlySlowTask.Reset();
		}

		bExportRequestInProgress = false;
	}
}


void FGridlyLocalizationServiceProvider::ExportForTargetToGridly(ULocalizationTarget* InLocalizationTarget, FHttpRequestCompleteDelegate& ReqDelegate, const FText& SlowTaskText, bool bIncTargetTranslation)
{
	TArray<FPolyglotTextData> PolyglotTextDatas;
	TSharedPtr<FLocTextHelper> LocTextHelperPtr;
	UERecords.Empty();
	GridlyRecords.Empty();


	if (FGridlyLocalizedText::GetAllTextAsPolyglotTextDatas(InLocalizationTarget, PolyglotTextDatas, LocTextHelperPtr))
	{
		size_t TotalRequests = 0;

		while (PolyglotTextDatas.Num() > 0)
		{
			const size_t ChunkSize = FMath::Min(GetMutableDefault<UGridlyGameSettings>()->ExportMaxRecordsPerRequest, PolyglotTextDatas.Num());
			const TArray<FPolyglotTextData> ChunkPolyglotTextDatas(PolyglotTextDatas.GetData(), ChunkSize);
			PolyglotTextDatas.RemoveAt(0, ChunkSize);
			const auto HttpRequest = CreateExportRequest(ChunkPolyglotTextDatas, LocTextHelperPtr, bIncTargetTranslation);
			HttpRequest->OnProcessRequestComplete() = ReqDelegate;
			ExportFromTargetRequestQueue.Enqueue(HttpRequest);
			for (int i = 0; i < ChunkPolyglotTextDatas.Num(); i++)
			{
				const FString& Key = ChunkPolyglotTextDatas[i].GetKey();  // Access the correct array
				const FString& Namespace = ChunkPolyglotTextDatas[i].GetNamespace();  // Access the correct array
				
				UERecords.Add(FGridlyTypeRecord(Key, Namespace));
			}

			TotalRequests++;
		}

		ExportForTargetEntriesUpdated = 0;

		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
		if (ExportFromTargetRequestQueue.Dequeue(HttpRequest))
		{
			if (!IsRunningCommandlet())
			{
				ExportForTargetToGridlySlowTask = MakeShareable(new FScopedSlowTask(static_cast<float>(TotalRequests), SlowTaskText));
				ExportForTargetToGridlySlowTask->MakeDialog();
			}

			bExportRequestInProgress = true;
			HttpRequest->ProcessRequest();
		}
	}
}

bool FGridlyLocalizationServiceProvider::HasRequestsPending() const
{
	return !ExportFromTargetRequestQueue.IsEmpty() || bExportRequestInProgress;
}

FHttpRequestCompleteDelegate FGridlyLocalizationServiceProvider::CreateExportNativeCultureDelegate()
{
	return FHttpRequestCompleteDelegate::CreateRaw(this, &FGridlyLocalizationServiceProvider::OnExportNativeCultureForTargetToGridly);
}

void FGridlyLocalizationServiceProvider::FetchGridlyCSV()
{
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FString ApiKey = GameSettings->ExportApiKey;
	const FString ViewId = GameSettings->ExportViewId;
	// URL for fetching the CSV from Gridly
	FStringFormatNamedArguments Args;
	Args.Add(TEXT("ViewId"), *ViewId);
	const FString GridlyURL = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/export"), Args);

	// Create the HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(GridlyURL);

	// Set the required headers, including the authorization
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/csv"));

	// Bind a callback to handle the response
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnGridlyCSVResponseReceived);

	// Send the request
	HttpRequest->ProcessRequest();
}

void FGridlyLocalizationServiceProvider::OnGridlyCSVResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to fetch Gridly CSV"));
		return;
	}

	// Retrieve the response content (CSV data)
	FString CSVContent = Response->GetContentAsString();

	// Parse the CSV data to extract records
	ParseCSVAndCreateRecords(CSVContent);
}


void FGridlyLocalizationServiceProvider::ParseCSVAndCreateRecords(const FString& CSVContent)
{
	const TCHAR QuoteChar = TEXT('"');
	const TCHAR Delimiter = TEXT(',');

	bool bInsideQuotes = false;
	FString CurrentField;
	TArray<FString> Fields;
	FString CurrentLine;

	// Buffer to store the accumulated lines in case of multi-line records
	TArray<FString> AccumulatedLines;

	int32 RecordIdColumnIndex = -1;
	int32 PathColumnIndex = -1;

	// First pass: determine which columns contain the Record ID and Path
	bool bFoundHeader = false;
	for (int32 i = 0; i < CSVContent.Len(); ++i)
	{
		TCHAR Char = CSVContent[i];

		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < CSVContent.Len() && CSVContent[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i;
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
		else
		{
			if (Char == QuoteChar)
			{
				bInsideQuotes = true;
			}
			else if (Char == Delimiter)
			{
				Fields.Add(CurrentField);
				CurrentField.Empty();
			}
			else if (Char == '\n' || Char == '\r')
			{
				// End of header line, process the column headers
				if (Fields.Num() > 0 || !CurrentField.IsEmpty())
				{
					Fields.Add(CurrentField);
					CurrentField.Empty();
				}

				if (!bFoundHeader)
				{
					for (int32 ColumnIndex = 0; ColumnIndex < Fields.Num(); ++ColumnIndex)
					{
						FString ColumnName = Fields[ColumnIndex].TrimQuotes();

						if (ColumnName.Equals(TEXT("Record ID"), ESearchCase::IgnoreCase))
						{
							RecordIdColumnIndex = ColumnIndex;
						}
						else if (ColumnName.Equals(TEXT("Path"), ESearchCase::IgnoreCase))
						{
							PathColumnIndex = ColumnIndex;
						}
					}

					bFoundHeader = true;
					Fields.Empty();
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
	}

	// Check if we found both necessary columns
	if (RecordIdColumnIndex == -1 || PathColumnIndex == -1)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to identify Record ID or Path columns in CSV."));
		return;
	}

	// Second pass: parse the actual records
	bInsideQuotes = false;
	Fields.Empty();
	CurrentField.Empty();
	for (int32 i = 0; i < CSVContent.Len(); ++i)
	{
		TCHAR Char = CSVContent[i];

		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < CSVContent.Len() && CSVContent[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i;
				}
				else
				{
					bInsideQuotes = false;
				}
			}
			else
			{
				CurrentField += Char;
			}
		}
		else
		{
			if (Char == QuoteChar)
			{
				bInsideQuotes = true;
			}
			else if (Char == Delimiter)
			{
				Fields.Add(CurrentField);
				CurrentField.Empty();
			}
			else if (Char == '\n' || Char == '\r')
			{
				if (Fields.Num() > 0 || !CurrentField.IsEmpty())
				{
					Fields.Add(CurrentField);
					CurrentField.Empty();
				}

				if (Fields.Num() > FMath::Max(RecordIdColumnIndex, PathColumnIndex))
				{
					FString RecordId = Fields[RecordIdColumnIndex].TrimQuotes();
					FString Path = Fields[PathColumnIndex].TrimQuotes();


					FGridlyTypeRecord NewRecord(RemoveNamespaceFromKey(RecordId), Path);

					if (NewRecord.Id != "Record ID") {
						GridlyRecords.Add(NewRecord);
					}
				}

				Fields.Empty();
			}
			else
			{
				CurrentField += Char;
			}
		}
	}

	// Handle the last line if needed
	if (Fields.Num() > 0 || !CurrentField.IsEmpty())
	{
		Fields.Add(CurrentField);
		if (Fields.Num() > FMath::Max(RecordIdColumnIndex, PathColumnIndex))
		{
			FString RecordId = Fields[RecordIdColumnIndex].TrimQuotes();
			FString Path = Fields[PathColumnIndex].TrimQuotes();


			FGridlyTypeRecord NewRecord(RemoveNamespaceFromKey(RecordId), Path);

			if (NewRecord.Id != "Record ID") {
				GridlyRecords.Add(NewRecord);
			}
		}
	}

	for (const FGridlyTypeRecord& Record : UERecords)
	{
		UE_LOG(LogTemp, Log, TEXT("UE Record ID: %s, Path: %s"), *Record.Id, *Record.Path);
	}
	

	// Log or further process the GridlyRecords array
	for (const FGridlyTypeRecord& Record : GridlyRecords)
	{
		UE_LOG(LogTemp, Log, TEXT("Gridly Record ID: %s, Path: %s"), *Record.Id, *Record.Path);
	}

	TArray<FString> RecordsToDelete;
	

	for (const FGridlyTypeRecord& GridlyRecord : GridlyRecords)
	{
		// Check if any UERecord has a matching path first
		bool PathFoundInUE = false;
		bool RecordIdFoundInUE = false;

		for (const FGridlyTypeRecord& UERecord : UERecords)
		{
			if (GridlyRecord.Path == UERecord.Path)
			{
				PathFoundInUE = true; // The path matches
				if (GridlyRecord.Id == UERecord.Id)
				{
					RecordIdFoundInUE = true; // The record ID matches as well for the same path
					break; // Both path and record ID match, no need to continue searching
				}
			}
		}

		// Only handle deletion if the path was found, but the ID was not found for that path
		if (PathFoundInUE && !RecordIdFoundInUE)
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("No match found for GridlyRecord: ID = %s, Path = %s. Adding to delete list."), *GridlyRecord.Id, *GridlyRecord.Path);

			// If the path is empty, we only add the record ID
			if (GridlyRecord.Path.Len() == 0)
			{
				RecordsToDelete.Add(GridlyRecord.Id);
			}
			// If the path starts with "blueprints/", add the ID with a comma prefix
			else if (GridlyRecord.Path.StartsWith(TEXT("blueprints/")))
			{
				RecordsToDelete.Add("," + GridlyRecord.Id);
			}
			else
			{
				// Otherwise, add the path and ID combination
				RecordsToDelete.Add(GridlyRecord.Path + "," + GridlyRecord.Id);
			}
		}
	}


	

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Number of Gridly records: %d"), GridlyRecords.Num());
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Number of UE records: %d"), UERecords.Num());


	// Optionally, pass this list for further processing
	DeleteRecordsFromGridly(RecordsToDelete);
}

void FGridlyLocalizationServiceProvider::DeleteRecordsFromGridly(const TArray<FString>& RecordsToDelete)
{
	const int32 MaxRecordsPerRequest = 1000;  // Maximum number of records per batch

	if (RecordsToDelete.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("No records to delete."));
		return;
	}
	// Initialize the counters
	CompletedBatches = 0;  // Reset the counter for completed batches
	TotalBatchesToProcess = FMath::CeilToInt(static_cast<float>(RecordsToDelete.Num()) / MaxRecordsPerRequest);


	// Split the records into batches of MaxRecordsPerRequest
	int32 TotalRecords = RecordsToDelete.Num();
	int32 TotalBatches = FMath::CeilToInt(static_cast<float>(TotalRecords) / MaxRecordsPerRequest);
	CompletedBatches = 0;  // Initialize the completed batch counter
	TotalBatchesToProcess = TotalBatches;  // Track the total number of batches

	for (int32 BatchIndex = 0; BatchIndex < TotalBatches; BatchIndex++)
	{
		// Create a new array for each batch
		TArray<FString> BatchRecords;

		int32 StartIndex = BatchIndex * MaxRecordsPerRequest;
		int32 EndIndex = FMath::Min(StartIndex + MaxRecordsPerRequest, TotalRecords); // Ensure not to exceed total records

		// Manually append the batch records
		for (int32 i = StartIndex; i < EndIndex; ++i)
		{
			BatchRecords.Add(RecordsToDelete[i]);
		}

		// Convert the batch to JSON and send the request
		FString JsonPayload;
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> JsonIds;

		for (const FString& RecordId : BatchRecords)
		{
			JsonIds.Add(MakeShared<FJsonValueString>(RecordId));
		}

		JsonObject->SetArrayField(TEXT("ids"), JsonIds);

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonPayload);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

		// Log the JSON payload for debugging
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("JSON Payload: %s"), *JsonPayload);

		const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
		const FString ApiKey = GameSettings->ExportApiKey;
		const FString ViewId = GameSettings->ExportViewId;

		FStringFormatNamedArguments Args;
		Args.Add(TEXT("ViewId"), *ViewId);
		const FString Url = FString::Format(TEXT("https://api.gridly.com/v1/views/{ViewId}/records"), Args);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetVerb(TEXT("DELETE"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
		HttpRequest->SetURL(Url);
		HttpRequest->SetContentAsString(JsonPayload);

		// Bind the response handler for each batch
		HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnDeleteRecordsResponse);

		HttpRequest->ProcessRequest();

		// Track the number of records requested for deletion
		ExportForTargetEntriesDeleted += BatchRecords.Num();

		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Delete request sent for %d records."), BatchRecords.Num());
	}
}

void FGridlyLocalizationServiceProvider::OnDeleteRecordsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Request.IsValid() || !Response.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("Invalid HTTP request or response."));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Invalid HTTP request or response.")));
		return;
	}

	// Increment the completed batch counter
	CompletedBatches++;

	if (bWasSuccessful && Response->GetResponseCode() == EHttpResponseCodes::NoContent)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Successfully deleted records."));

		// Only show the success message when all batches are done
		if (CompletedBatches == TotalBatchesToProcess && !IsRunningCommandlet())
		{
			// Prepare and show a success message dialog
			FString Message = FString::Printf(TEXT("Number of entries deleted: %llu"), ExportForTargetEntriesDeleted);

			UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
		}
	}
	else
	{
		// Handle any failure cases
		FString ErrorMessage = FString::Printf(TEXT("Failed to delete records. HTTP Code: %d, Response: %s"),
			Response->GetResponseCode(), *Response->GetContentAsString());

		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("%s"), *ErrorMessage);

		// Display a failure message dialog when all batches are done
		if (CompletedBatches == TotalBatchesToProcess && !IsRunningCommandlet())
		{
			FString DialogMessage = FString::Printf(TEXT("Error during record deletion.\nHTTP Code: %d\nResponse: %s"),
				Response->GetResponseCode(), *Response->GetContentAsString());

			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogMessage));
		}
	}
}



FString FGridlyLocalizationServiceProvider::RemoveNamespaceFromKey(FString& InputString)
{

	// Find the first comma and chop the string from the right if a comma exists
	int32 CommaIndex;
	if (InputString.FindChar(TEXT(','), CommaIndex))
	{
		return InputString.RightChop(CommaIndex + 1);
	}

	// Return the string as-is if no comma is found
	return InputString;
}


#undef LOCTEXT_NAMESPACE