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
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "GridlyCultureConverter.h"
#include "LocalizationConfigurationScript.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IHttpRequest.h"
#include "Internationalization/Text.h"
#include "LocalizationTargetTypes.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/StringTableCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"


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
	TSharedPtr<FUICommandInfo> DownloadSourceChangesFromGridly;

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
	UI_COMMAND(DownloadSourceChangesFromGridly, "Download Source Changes",
		"Downloads source changes from Gridly and updates string tables with CSV import.", EUserInterfaceActionType::Button, FInputChord());
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

		CommandList->MapAction(FGridlyLocalizationTargetEditorCommands::Get().DownloadSourceChangesFromGridly,
			FExecuteAction::CreateRaw(this, &FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridly,
				LocalizationTarget, bIsTargetSet));
		ToolbarBuilder.AddToolBarButton(
			FGridlyLocalizationTargetEditorCommands::Get().DownloadSourceChangesFromGridly, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FGridlyStyle::GetStyleSetName(),
				"Gridly.ImportAction"));
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
			"This will overwrite your source strings on Gridly with the data in your UE project. Are you sure you wish to export?"));

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
			"This will overwrite all your source strings AND translations on Gridly with the data in your UE project. Are you sure you wish to export?"));

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
	// Set the flag to true at the beginning of the process
	bHasDeletesPending = true;
	
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
		bHasDeletesPending = false; // Reset flag on failure
		return;
	}

	// Retrieve the response content (CSV data)
	FString CSVContent = Response->GetContentAsString();

	// Parse the CSV data to extract records
	ParseCSVAndCreateRecords(CSVContent);
}

void FGridlyLocalizationServiceProvider::ParseCSVAndCreateRecords(const FString& CSVContent)
{
	// Don't reset the flag here, it will be reset in DeleteRecordsFromGridly if there are no records to delete
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	
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
		bHasDeletesPending = false; // Reset flag if we can't identify the columns
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

		// Only handle deletion if the path was found, but the ID was not found for that path or path not found in UE
		if ((PathFoundInUE && !RecordIdFoundInUE ) || !PathFoundInUE)
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("No match found for GridlyRecord: ID = %s, Path = %s. Adding to delete list."), *GridlyRecord.Id, *GridlyRecord.Path);

			// If the path is empty or used combine namespace and ID is false, we only add the record ID
			if (GridlyRecord.Path.Len() == 0 || !GameSettings->bUseCombinedNamespaceId)
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
	if (RecordsToDelete.Num() > 0)
	{
		DeleteRecordsFromGridly(RecordsToDelete);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("No records to delete."));
		bHasDeletesPending = false; // Reset flag if there are no records to delete
	}
}

void FGridlyLocalizationServiceProvider::DeleteRecordsFromGridly(const TArray<FString>& RecordsToDelete)
{
	const int32 MaxRecordsPerRequest = 1000;  // Maximum number of records per batch
	UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("DeleteRecordsFromGridly CALLED"));

	if (RecordsToDelete.Num() == 0)
	{
		bHasDeletesPending = false;
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("No records to delete."));
		return;
	}
	bHasDeletesPending = true;
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
			// Clean up the record ID to prevent duplication
			FString CleanRecordId = RecordsToDelete[i];
			// Remove any duplicate commas and spaces
			CleanRecordId = CleanRecordId.Replace(TEXT(",,"), TEXT(","));
			CleanRecordId = CleanRecordId.Replace(TEXT(" ,"), TEXT(","));
			CleanRecordId = CleanRecordId.Replace(TEXT(", "), TEXT(","));
			
			BatchRecords.Add(CleanRecordId);
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
		bHasDeletesPending = false;  // Reset flag on invalid request/response
		return;
	}

	// Increment the completed batch counter
	CompletedBatches++;

	if (bWasSuccessful && Response->GetResponseCode() == EHttpResponseCodes::NoContent)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Successfully deleted records."));

		// Only show the success message when all batches are done
		if (CompletedBatches == TotalBatchesToProcess)
		{
			bHasDeletesPending = false;

			if (!IsRunningCommandlet())
			{
				// Show dialog only in editor mode
				FString Message = FString::Printf(TEXT("Number of entries deleted: %llu"), ExportForTargetEntriesDeleted);
				UE_LOG(LogGridlyEditor, Log, TEXT("%s"), *Message);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
			}
			else
			{
				UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("Commandlet: Deleted %llu records from Gridly."), ExportForTargetEntriesDeleted);
			}
		}
	}
	else
	{
		// Handle any failure cases
		FString ErrorMessage = FString::Printf(TEXT("Failed to delete records. HTTP Code: %d, Response: %s"),
			Response->GetResponseCode(), *Response->GetContentAsString());

		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("%s"), *ErrorMessage);

		// Reset the flag when all batches are done, regardless of success or failure
		if (CompletedBatches == TotalBatchesToProcess)
		{
			bHasDeletesPending = false;
			
			if (!IsRunningCommandlet())
			{
				FString DialogMessage = FString::Printf(TEXT("Error during record deletion.\nHTTP Code: %d\nResponse: %s"),
					Response->GetResponseCode(), *Response->GetContentAsString());

				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogMessage));
			}
		}
	}
}

bool FGridlyLocalizationServiceProvider::HasDeleteRequestsPending() const
{
	return bHasDeletesPending;
}

void FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridly(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, bool bIsTargetSet)
{
	check(LocalizationTarget.IsValid());

	const EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("ConfirmSourceChangesText",
			"🔄 Download Source Changes from Gridly\n\n"
			"This feature will:\n"
			"• Download source strings from Gridly per namespace\n"
			"• Generate CSV files for each string table\n"
			"• Store files in: [Project]/Saved/Temp/GridlySourceChanges/\n\n"
			"⚠️ WARNING: This may modify source strings in your localization files.\n"
			"Review all changes before committing to version control.\n\n"
			"Are you sure you wish to proceed?"));

	if (!bIsTargetSet && MessageReturn == EAppReturnType::Yes)
	{
		// Get the native culture for source strings
		FString NativeCulture;
		if (LocalizationTarget->Settings.SupportedCulturesStatistics.IsValidIndex(LocalizationTarget->Settings.NativeCultureIndex))
		{
			const FCultureStatistics CultureStats = LocalizationTarget->Settings.SupportedCulturesStatistics[LocalizationTarget->Settings.NativeCultureIndex];
			NativeCulture = CultureStats.CultureName;
		}
		else
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No native culture found for target: %s"), *LocalizationTarget->Settings.Name);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No native culture found for this localization target.")));
			return;
		}

		// Check if we have any supported cultures
		if (LocalizationTarget->Settings.SupportedCulturesStatistics.Num() == 0)
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No supported cultures found for target: %s"), *LocalizationTarget->Settings.Name);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No supported cultures found for this localization target.")));
			return;
		}

		// Download source changes from Gridly
		DownloadSourceChangesFromGridlyInternal(LocalizationTarget, NativeCulture);
	}
}

void FGridlyLocalizationServiceProvider::DownloadSourceChangesFromGridlyInternal(TWeakObjectPtr<ULocalizationTarget> LocalizationTarget, const FString& NativeCulture)
{
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	const FString ApiKey = GameSettings->ImportApiKey;

	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No import API key configured"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No import API key configured.\n\nPlease configure the Gridly plugin settings:\n1. Go to Project Settings > Plugins > Gridly\n2. Set the Import API Key\n3. Add at least one Import View ID")));
		return;
	}

	// Get the first view ID for import
	if (GameSettings->ImportFromViewIds.Num() == 0 || GameSettings->ImportFromViewIds[0].IsEmpty())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No import view ID configured"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ No import view ID configured.\n\nPlease configure the Gridly plugin settings:\n1. Go to Project Settings > Plugins > Gridly\n2. Add at least one Import View ID")));
		return;
	}

	const FString ViewId = GameSettings->ImportFromViewIds[0];
	const FString Url = FString::Printf(TEXT("https://api.gridly.com/v1/views/%s/records"), *ViewId);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("ApiKey %s"), *ApiKey));
	HttpRequest->SetURL(Url);

	// Store the localization target and culture for the callback
	CurrentSourceDownloadTarget = LocalizationTarget;
	CurrentSourceDownloadCulture = NativeCulture;

	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGridlyLocalizationServiceProvider::OnDownloadSourceChangesFromGridly);
	HttpRequest->ProcessRequest();

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🔄 Downloading source changes from Gridly for target: %s, culture: %s"), 
		*LocalizationTarget->Settings.Name, *NativeCulture);
}

void FGridlyLocalizationServiceProvider::OnDownloadSourceChangesFromGridly(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to download source changes from Gridly"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ Failed to download source changes from Gridly. Please check your API key and view ID.")));
		return;
	}

	const FString ResponseContent = Response->GetContentAsString();
	
	// Parse the JSON response to get the records
	TArray<TSharedPtr<FJsonValue>> RecordsArray;
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(ResponseContent);
	
	if (!FJsonSerializer::Deserialize(JsonReader, RecordsArray) || RecordsArray.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to parse JSON response from Gridly"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("❌ Failed to parse response from Gridly.")));
		return;
	}

	// Group records by namespace (path column)
	TMap<FString, TArray<FGridlySourceRecord>> NamespaceRecords;
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();

	for (const TSharedPtr<FJsonValue>& RecordValue : RecordsArray)
	{
		const TSharedPtr<FJsonObject>* RecordObject;
		if (!RecordValue->TryGetObject(RecordObject) || !RecordObject->IsValid())
		{
			continue;
		}

		FGridlySourceRecord SourceRecord;
		
		// Extract record ID
		FString RecordId;
		if ((*RecordObject)->TryGetStringField(FString(TEXT("id")), RecordId))
		{
			SourceRecord.RecordId = RecordId;
		}

		// Extract path (namespace)
		FString Path;
		if ((*RecordObject)->TryGetStringField(FString(TEXT("path")), Path))
		{
			SourceRecord.Path = Path;
		}

		// Extract source text from the native culture column
		const TArray<TSharedPtr<FJsonValue>>* CellsArray;
		if ((*RecordObject)->TryGetArrayField(TEXT("cells"), CellsArray))
		{
			for (const TSharedPtr<FJsonValue>& CellValue : *CellsArray)
			{
				const TSharedPtr<FJsonObject>* CellObject;
				if (CellValue->TryGetObject(CellObject) && CellObject->IsValid())
				{
					FString ColumnId;
					FString Value;
					
					if ((*CellObject)->TryGetStringField(FString(TEXT("columnId")), ColumnId) && 
						(*CellObject)->TryGetStringField(FString(TEXT("value")), Value))
					{
						// Check if this is the source language column
						if (ColumnId.StartsWith(GameSettings->SourceLanguageColumnIdPrefix))
						{
							const FString GridlyCulture = ColumnId.RightChop(GameSettings->SourceLanguageColumnIdPrefix.Len());
							FString Culture;
							
							// Convert Gridly culture to UE culture
							if (FGridlyCultureConverter::ConvertFromGridly(TArray<FString>(), GridlyCulture, Culture))
							{
								// Check if this matches our native culture
								if (Culture == CurrentSourceDownloadCulture)
								{
									SourceRecord.SourceText = Value;
									break;
								}
							}
						}
					}
				}
			}
		}

		// Only add records that have valid data
		if (!SourceRecord.RecordId.IsEmpty() && !SourceRecord.SourceText.IsEmpty())
		{
			FString Namespace = SourceRecord.Path;
			
			// Handle combined namespace key format
			if (GameSettings->bUseCombinedNamespaceId)
			{
				FString Key;
				if (SourceRecord.RecordId.Split(TEXT(","), &Namespace, &Key))
				{
					SourceRecord.RecordId = Key;
				}
			}

			// Clean up namespace
			Namespace = Namespace.Replace(TEXT(" "), TEXT(""));
			
			if (!Namespace.IsEmpty())
			{
				NamespaceRecords.FindOrAdd(Namespace).Add(SourceRecord);
			}
		}
	}

	// Generate CSV files for each namespace and update string tables
	ProcessSourceChangesForNamespaces(NamespaceRecords);
}

void FGridlyLocalizationServiceProvider::ProcessSourceChangesForNamespaces(const TMap<FString, TArray<FGridlySourceRecord>>& NamespaceRecords)
{
	if (!CurrentSourceDownloadTarget.IsValid())
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("Invalid localization target for source changes processing"));
		return;
	}

	ULocalizationTarget* LocalizationTarget = CurrentSourceDownloadTarget.Get();
	const FString TargetName = LocalizationTarget->Settings.Name;
	
	// Create temporary directory for CSV files
	const FString TempDir = FPaths::ProjectSavedDir() / TEXT("Temp") / TEXT("GridlySourceChanges") / TargetName;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	if (!PlatformFile.DirectoryExists(*TempDir))
	{
		PlatformFile.CreateDirectoryTree(*TempDir);
	}

	int32 ProcessedNamespaces = 0;
	int32 TotalNamespaces = NamespaceRecords.Num();

	for (const auto& NamespacePair : NamespaceRecords)
	{
		const FString& Namespace = NamespacePair.Key;
		const TArray<FGridlySourceRecord>& Records = NamespacePair.Value;

		ProcessedNamespaces++;
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📊 Processing namespace %d/%d: %s (%d records)"), 
			ProcessedNamespaces, TotalNamespaces, *Namespace, Records.Num());

		// Generate CSV content
		FString CSVContent = TEXT("Key,SourceString\n");
		
		for (const FGridlySourceRecord& Record : Records)
		{
			// Escape quotes in the source text
			FString EscapedSourceText = Record.SourceText;
			EscapedSourceText = EscapedSourceText.Replace(TEXT("\""), TEXT("\"\""));
			
			CSVContent += FString::Printf(TEXT("\"%s\",\"%s\"\n"), *Record.RecordId, *EscapedSourceText);
		}

		// Write CSV file
		const FString CSVFilePath = TempDir / FString::Printf(TEXT("%s.csv"), *Namespace);
		
		if (FFileHelper::SaveStringToFile(CSVContent, *CSVFilePath))
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Generated CSV file for namespace '%s': %s"), *Namespace, *CSVFilePath);
			
			// Import the CSV into the string table
			ImportCSVToStringTable(LocalizationTarget, Namespace, CSVFilePath);
		}
		else
		{
			UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to write CSV file for namespace '%s': %s"), *Namespace, *CSVFilePath);
		}
	}

	// Show completion message
	FString Message = FString::Printf(TEXT("✅ Source changes processing completed!\n\n📊 Processed %d namespaces\n📁 CSV files saved to: %s\n\n🎉 String tables updated!\n• Source strings have been imported directly into string table assets\n• String table UI should now show the updated/new entries\n• String tables are marked as modified and need to be saved\n\n📝 Next Steps:\n• Review changes in the string table editor\n• Save the modified string table assets\n• Run 'Gather Text' from the Localization Dashboard to update manifest files\n• Commit changes to version control\n\n⚠️ Note: This feature modifies source strings. Review changes before committing."), 
		ProcessedNamespaces, *TempDir);
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("%s"), *Message);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

bool FGridlyLocalizationServiceProvider::ImportCSVToStringTable(ULocalizationTarget* LocalizationTarget, const FString& Namespace, const FString& CSVFilePath)
{
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📄 CSV file ready for import: %s"), *CSVFilePath);
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🏷️ Namespace: %s, Target: %s"), *Namespace, *LocalizationTarget->Settings.Name);
	
	// Parse the CSV file
	TArray<FString> CSVLines;
	if (!FFileHelper::LoadFileToStringArray(CSVLines, *CSVFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to read CSV file: %s"), *CSVFilePath);
		return false;
	}

	if (CSVLines.Num() < 2) // Need at least header + 1 data row
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ CSV file is empty or has no data rows: %s"), *CSVFilePath);
		return false;
	}

	// Parse CSV header
	FString HeaderLine = CSVLines[0];
	TArray<FString> HeaderFields;
	HeaderLine.ParseIntoArray(HeaderFields, TEXT(","));
	
	if (HeaderFields.Num() < 2)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Invalid CSV header format: %s"), *HeaderLine);
		return false;
	}

	// Validate header
	if (!HeaderFields[0].Contains(TEXT("Key")) || !HeaderFields[1].Contains(TEXT("SourceString")))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ CSV header must contain 'Key' and 'SourceString' columns"));
		return false;
	}

	// Parse CSV data
	TMap<FString, FString> KeyValuePairs;
	for (int32 i = 1; i < CSVLines.Num(); ++i)
	{
		FString Line = CSVLines[i];
		if (Line.IsEmpty())
		{
			continue;
		}

		// Simple CSV parsing (handles quoted fields)
		TArray<FString> Fields;
		ParseCSVLine(Line, Fields);
		
		if (Fields.Num() >= 2)
		{
			FString Key = Fields[0].TrimQuotes();
			FString Value = Fields[1].TrimQuotes();
			
			if (!Key.IsEmpty() && !Value.IsEmpty())
			{
				KeyValuePairs.Add(Key, Value);
			}
		}
	}

	if (KeyValuePairs.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ No valid key-value pairs found in CSV: %s"), *CSVFilePath);
		return false;
	}

	// Get the localization target's manifest path
	const FString ConfigFilePath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocalizationTarget);
	const FString SectionName = TEXT("CommonSettings");
	
	FString SourcePath;
	if (!GConfig->GetString(*SectionName, TEXT("SourcePath"), SourcePath, ConfigFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No source path specified in config"));
		return false;
	}
	
	FString ManifestName;
	if (!GConfig->GetString(*SectionName, TEXT("ManifestName"), ManifestName, ConfigFilePath))
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ No manifest name specified in config"));
		return false;
	}
	
	// Determine manifest path
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
	
	ManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);

	// Import into string table using the passed localization target
	bool bSuccess = ImportKeyValuePairsToStringTable(LocalizationTarget, Namespace, KeyValuePairs);
	
	if (bSuccess)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Successfully imported %d entries for namespace '%s'"), 
		KeyValuePairs.Num(), *Namespace);
	}
	else
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to import entries for namespace '%s'"), *Namespace);
	}
	
	return bSuccess;
}



void FGridlyLocalizationServiceProvider::ParseCSVLine(const FString& Line, TArray<FString>& OutFields)
{
	OutFields.Empty();
	
	const TCHAR QuoteChar = TEXT('"');
	const TCHAR Delimiter = TEXT(',');
	
	bool bInsideQuotes = false;
	FString CurrentField;
	
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		TCHAR Char = Line[i];
		
		if (bInsideQuotes)
		{
			if (Char == QuoteChar)
			{
				if (i + 1 < Line.Len() && Line[i + 1] == QuoteChar)
				{
					CurrentField += QuoteChar;
					++i; // Skip the next quote
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
				OutFields.Add(CurrentField);
				CurrentField.Empty();
			}
			else
			{
				CurrentField += Char;
			}
		}
	}
	
	// Add the last field
	OutFields.Add(CurrentField);
}

bool FGridlyLocalizationServiceProvider::ImportKeyValuePairsToStringTable(ULocalizationTarget* LocalizationTarget, const FString& Namespace, const TMap<FString, FString>& KeyValuePairs)
{
	if (KeyValuePairs.Num() == 0)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ No key-value pairs to import for namespace: %s"), *Namespace);
		return true;
	}

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🔄 Importing %d entries for namespace '%s' using direct string table modification"), KeyValuePairs.Num(), *Namespace);

	if (!LocalizationTarget)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Invalid localization target"));
		return false;
	}

	// Find or create string table asset for this namespace
	UStringTable* StringTable = FindOrCreateStringTable(Namespace);
	if (!StringTable)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to find or create string table for namespace: %s"), *Namespace);
		return false;
	}
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🎯 Working with string table: %s"), *StringTable->GetPathName());

	// Import each key-value pair directly into the string table
	int32 ImportedCount = 0;
	int32 UpdatedCount = 0;
	int32 CreatedCount = 0;

	for (const auto& KeyValuePair : KeyValuePairs)
	{
		const FString& Key = KeyValuePair.Key;
		const FString& Value = KeyValuePair.Value;

		// Use the string table's mutable interface to set source strings
		FStringTable& MutableStringTable = StringTable->GetMutableStringTable().Get();
		
		// Check if entry already exists
		FString ExistingValue;
		bool bExists = MutableStringTable.GetSourceString(Key, ExistingValue);
		
		if (bExists)
		{
			// Update existing entry
			MutableStringTable.SetSourceString(Key, Value);
			UpdatedCount++;
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📝 Updated existing entry: %s = %s (was: %s)"), *Key, *Value, *ExistingValue);
		}
		else
		{
			// Create new entry
			MutableStringTable.SetSourceString(Key, Value);
			CreatedCount++;
			UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("🆕 Created new entry: %s = %s"), *Key, *Value);
		}
		
		ImportedCount++;
	}

	// Mark the string table as modified and save it
	StringTable->Modify(true);
	StringTable->MarkPackageDirty();
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("💾 String table marked as dirty and modified: %s"), *StringTable->GetPathName());
	
	// Mark the string table as modified (user will save manually)
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📝 String table marked as modified: %s"), *StringTable->GetPathName());
	
	// Mark the asset as dirty so user knows it needs saving
	StringTable->MarkPackageDirty();

	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Imported %d/%d entries for namespace '%s' (%d updated, %d created)"), 
		ImportedCount, KeyValuePairs.Num(), *Namespace, UpdatedCount, CreatedCount);
	
	return true;
}

UStringTable* FGridlyLocalizationServiceProvider::FindOrCreateStringTable(const FString& Namespace)
{
	// Try to find existing string table
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	
	FARFilter Filter;
	Filter.ClassPaths.Add(UStringTable::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);
	
	// Look for a string table with the exact namespace match
	for (const FAssetData& AssetData : AssetList)
	{
		UStringTable* StringTable = Cast<UStringTable>(AssetData.GetAsset());
		if (StringTable)
		{
			// Get the string table name and check for exact namespace match
			FString StringTableName = StringTable->GetName();
			FString StringTablePath = StringTable->GetPathName();
			
			// Check for exact namespace match in the name or path
			if (StringTableName == Namespace || 
				StringTableName.EndsWith(FString::Printf(TEXT("_%s"), *Namespace)) ||
				StringTablePath.Contains(FString::Printf(TEXT("/%s."), *Namespace)) ||
				StringTablePath.Contains(FString::Printf(TEXT("/%s/"), *Namespace)))
			{
				UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📋 Found existing string table: %s for namespace: %s"), *StringTable->GetPathName(), *Namespace);
				return StringTable;
			}
		}
	}
	
	// Create a new string table asset for this namespace
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("📋 Creating new string table for namespace: %s"), *Namespace);
	
	// Get the save path from plugin settings
	const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
	FString PackagePath = GameSettings->StringTableSavePath;
	
	// Fallback to default path if setting is empty
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Localization/StringTables");
		UE_LOG(LogGridlyLocalizationServiceProvider, Warning, TEXT("⚠️ StringTableSavePath is empty, using default path: %s"), *PackagePath);
	}
	
	FString AssetName = Namespace;
	
	// Ensure the package path exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString FullPath = FPaths::Combine(*FPaths::ProjectContentDir(), *PackagePath);
	if (!PlatformFile.DirectoryExists(*FullPath))
	{
		PlatformFile.CreateDirectoryTree(*FullPath);
	}
	
	// Create the asset
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName));
	if (!Package)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to create package for string table: %s"), *AssetName);
		return nullptr;
	}
	
	UStringTable* StringTable = NewObject<UStringTable>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!StringTable)
	{
		UE_LOG(LogGridlyLocalizationServiceProvider, Error, TEXT("❌ Failed to create string table asset: %s"), *AssetName);
		return nullptr;
	}
	
	// Register the asset with the asset registry
	FAssetRegistryModule::AssetCreated(StringTable);
	
	UE_LOG(LogGridlyLocalizationServiceProvider, Log, TEXT("✅ Created new string table asset: %s"), *StringTable->GetPathName());
	return StringTable;
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