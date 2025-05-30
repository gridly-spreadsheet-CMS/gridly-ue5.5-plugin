// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridlyImportExportCommandlet.h"
#include "GridlyLocalizationServiceProvider.h"
#include "Modules/ModuleManager.h"
#include "ILocalizationServiceModule.h"
#include "LocalizationModule.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "LocalizationConfigurationScript.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/Class.h"
#include <GridlyGameSettings.h>

DEFINE_LOG_CATEGORY_STATIC(LogGridlyImportExportCommandlet, Log, All);

#define LOCTEXT_NAMESPACE "GridlyImportExportCommandlet"

/**
*	UGridlyImportExportCommandlet
*/
int32 UGridlyImportExportCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Load localization module and its dependencies
	FModuleManager::Get().LoadModule(TEXT("LocalizationDashboard"));

	ILocalizationServiceModule& LocServiceModule = ILocalizationServiceModule::Get();
	LocServiceModule.SetProvider("Gridly");

	ILocalizationServiceProvider& LocServProvider = ILocalizationServiceModule::Get().GetProvider();

	const bool bCanUseGridly = LocServProvider.IsEnabled() && LocServProvider.IsAvailable() && LocServProvider.GetName().ToString() == TEXT("Gridly");

	FGridlyLocalizationServiceProvider* GridlyProvider = bCanUseGridly ? static_cast<FGridlyLocalizationServiceProvider*>(&LocServProvider) : nullptr;
	if (!GridlyProvider)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("Unable to retrieve Gridly Provider."));
		return -1;
	}

	// Set config
	FString ConfigPath;
	if (const FString* ConfigParamVal = ParamVals.Find(FString(TEXT("Config"))))
	{
		ConfigPath = FConfigCacheIni::NormalizeConfigIniPath(*ConfigParamVal);
	}
	else
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No config specified."));
		return -1;
	}

	// Reading ExportAllGameTarget argument
	FString* ExportAllGameTargetPtr = ParamVals.Find(FString(TEXT("ExportAllGameTarget")));

	// Set config section
	FString SectionName;
	if (const FString* ConfigSectionParamVal = ParamVals.Find(FString(TEXT("Section"))))
	{
		SectionName = *ConfigSectionParamVal;
	}
	else
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}

	bool bDoImport = false;
	GetBoolFromConfig(*SectionName, TEXT("bImportLoc"), bDoImport, ConfigPath);

	bool bDoExport = false;
	GetBoolFromConfig(*SectionName, TEXT("bExportLoc"), bDoExport, ConfigPath);

	if (!bDoImport && !bDoExport)
	{
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("Import/Export operation not detected.  Use bExportLoc or bImportLoc in config section."));
		return -1;
	}

	const TArray<ULocalizationTarget*> LocalizationTargets = ULocalizationSettings::GetGameTargetSet()->TargetObjects;
	//ULocalizationTarget* FirstLocTarget = LocalizationTargets.Num() > 0 ? LocalizationTargets[0]: nullptr;
	for (ULocalizationTarget* LocTarget : LocalizationTargets)
	{
		// Do something with LocTarget
		if (LocTarget != nullptr)
		{
			if (bDoImport)
			{
				// List all cultures (even the native one in case some native translations have been modified in Gridly) to download
				TArray<FString> Cultures;
				for (int ItCulture = 0; ItCulture < LocTarget->Settings.SupportedCulturesStatistics.Num(); ItCulture++)
				{
					if (ItCulture != LocTarget->Settings.NativeCultureIndex)
					{
						const FCultureStatistics CultureStats = LocTarget->Settings.SupportedCulturesStatistics[ItCulture];
						Cultures.Add(CultureStats.CultureName);
					}
				}

				// Download cultures from Gridly
				CulturesToDownload.Append(Cultures);
				for (const FString& CultureName : Cultures)
				{
					ILocalizationServiceProvider& Provider = ILocalizationServiceModule::Get().GetProvider();
					TSharedRef<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadTargetFileOp =
						ILocalizationServiceOperation::Create<FDownloadLocalizationTargetFile>();
					DownloadTargetFileOp->SetInTargetGuid(LocTarget->Settings.Guid);
					DownloadTargetFileOp->SetInLocale(CultureName);

					FString Path = FPaths::ProjectSavedDir() / "Temp" / "Game" / LocTarget->Settings.Name / CultureName /
						LocTarget->Settings.Name + ".po";
					FPaths::MakePathRelativeTo(Path, *FPaths::ProjectDir());
					FString NormalizedPath = FPaths::ConvertRelativePathToFull(Path);
					DownloadTargetFileOp->SetInRelativeOutputFilePathAndName(Path);

					auto OperationCompleteDelegate = FLocalizationServiceOperationComplete::CreateUObject(this,
						&UGridlyImportExportCommandlet::OnDownloadComplete, false);

					Provider.Execute(DownloadTargetFileOp, TArray<FLocalizationServiceTranslationIdentifier>(),
						ELocalizationServiceOperationConcurrency::Synchronous, OperationCompleteDelegate);
				}

				// Wait for all downloads
				while (CulturesToDownload.Num())
				{
					FPlatformProcess::Sleep(0.4f);
					FHttpModule::Get().GetHttpManager().Tick(-1.f);
				}

				// Run task to import po files, it will be done on the base folder and import all po files data generated after downloading data from gridly
				if (CulturesToDownload.Num() == 0 && DownloadedFiles.Num() > 0)
				{
					const FString& DlPoFile = DownloadedFiles[0]; // retrieve first po file to deduce the base folder
					const FString TargetName = FPaths::GetBaseFilename(DlPoFile);
					const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);

					const FString DirectoryPath = FPaths::GetPath(DlPoFile);
					const FString DownloadBasePath = FPaths::GetPath(DirectoryPath);

					// Create commandlet task to Import texts
					// Note that we could simply "Import all PO files" using a call to PortableObjectPipeline::ImportAll(...), though
					//		using tasks we are able to easily add/remove call to existing localization functionalities
					TArray<LocalizationCommandletExecution::FTask> Tasks;
					const bool ShouldUseProjectFile = !Target->IsMemberOfEngineTargetSet();

					// Normalize Import config path
					FString ImportScriptPath = LocalizationConfigurationScript::GetImportTextConfigPath(Target, TOptional<FString>());
					ImportScriptPath = FConfigCacheIni::NormalizeConfigIniPath(ImportScriptPath);
					LocalizationConfigurationScript::GenerateImportTextConfigFile(Target, TOptional<FString>(), DownloadBasePath).WriteWithSCC(ImportScriptPath);
					Tasks.Add(LocalizationCommandletExecution::FTask(LOCTEXT("ImportTaskName", "Import Translations"), ImportScriptPath, ShouldUseProjectFile));

					// Normalize Report config path
					FString ReportScriptPath = LocalizationConfigurationScript::GetWordCountReportConfigPath(Target);
					ReportScriptPath = FConfigCacheIni::NormalizeConfigIniPath(ReportScriptPath);
					LocalizationConfigurationScript::GenerateWordCountReportConfigFile(Target).WriteWithSCC(ReportScriptPath);
					Tasks.Add(LocalizationCommandletExecution::FTask(LOCTEXT("ReportTaskName", "Generate Reports"), ReportScriptPath, ShouldUseProjectFile));


					// Function will block until all tasks have been run
					BlockingRunLocCommandletTask(Tasks);
				}

				// Cleanup
				CulturesToDownload.Empty();
				DownloadedFiles.Empty();
			}

			if (bDoExport)
			{
				UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("Running gather text task before exporting to Gridly."));
				// Generate gather config file
				FString GatherScriptPath = LocalizationConfigurationScript::GetGatherTextConfigPath(LocTarget);
				GatherScriptPath = FConfigCacheIni::NormalizeConfigIniPath(GatherScriptPath);

				LocalizationConfigurationScript::GenerateGatherTextConfigFile(LocTarget).WriteWithSCC(GatherScriptPath);

				const bool bUseProjectFile = !LocTarget->IsMemberOfEngineTargetSet();

				LocalizationCommandletExecution::FTask GatherTask(
					LOCTEXT("GatherTaskName", "Gather Text"),
					GatherScriptPath,
					bUseProjectFile
				);

				// Run Gather before Export
				BlockingRunLocCommandletTask({ GatherTask });

				FHttpRequestCompleteDelegate ReqDelegate = GridlyProvider->CreateExportNativeCultureDelegate();
				const FText SlowTaskText = LOCTEXT("ExportNativeCultureForTargetToGridlyText", "Exporting native culture for target to Gridly");

				GridlyProvider->ExportForTargetToGridly(LocTarget, ReqDelegate, SlowTaskText);

				// Wait for export requests to complete
				while (GridlyProvider->HasRequestsPending())
				{
					FPlatformProcess::Sleep(0.4f);
					FHttpModule::Get().GetHttpManager().Tick(-1.f);
				}

				const UGridlyGameSettings* GameSettings = GetMutableDefault<UGridlyGameSettings>();
				if (GameSettings && GameSettings->bSyncRecords)
				{
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("Fetching Gridly CSV to check for stale records to delete..."));
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("First check: HasDeleteRequestsPending = %s"),
						GridlyProvider->HasDeleteRequestsPending() ? TEXT("true") : TEXT("false"));

					// Wait for delete requests to finish
					while (GridlyProvider->HasDeleteRequestsPending())
					{
						FPlatformProcess::Sleep(0.4f);
						FHttpModule::Get().GetHttpManager().Tick(-1.f);
					}
					UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("All record deletions completed."));

				}

			}
		}


		if (ExportAllGameTargetPtr && *ExportAllGameTargetPtr == "false") {
			break;
		}
	}
	return 0;
}

void UGridlyImportExportCommandlet::OnDownloadComplete(const FLocalizationServiceOperationRef& Operation, ELocalizationServiceOperationCommandResult::Type Result, bool bIsTargetSet)
{
	// do like in FGridlyLocalizationServiceProvider::OnImportCultureForTargetFromGridly
	TSharedPtr<FDownloadLocalizationTargetFile, ESPMode::ThreadSafe> DownloadLocalizationTargetOp = StaticCastSharedRef<FDownloadLocalizationTargetFile>(Operation);
	CulturesToDownload.Remove(DownloadLocalizationTargetOp->GetInLocale());

	if (Result != ELocalizationServiceOperationCommandResult::Succeeded)
	{
		const FText ErrorMessage = DownloadLocalizationTargetOp->GetOutErrorText();
		UE_LOG(LogGridlyImportExportCommandlet, Error, TEXT("%s"), *ErrorMessage.ToString());
	}

	const FString TargetName = FPaths::GetBaseFilename(DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

	const auto Target = ILocalizationModule::Get().GetLocalizationTargetByName(TargetName, false);
	const FString AbsoluteFilePathAndName = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / DownloadLocalizationTargetOp->GetInRelativeOutputFilePathAndName());

	DownloadedFiles.Add(AbsoluteFilePathAndName);
}

void UGridlyImportExportCommandlet::BlockingRunLocCommandletTask(const TArray<LocalizationCommandletExecution::FTask>& Tasks)
{
	for (const LocalizationCommandletExecution::FTask& LocTask : Tasks)
	{
		TSharedPtr<FLocalizationCommandletProcess> CommandletProcess = FLocalizationCommandletProcess::Execute(LocTask.ScriptPath, LocTask.ShouldUseProjectFile);

		if (CommandletProcess.IsValid())
		{
			UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("=== Starting Task [%s] ==="), *LocTask.Name.ToString());

			FProcHandle CurrentProcessHandle = CommandletProcess->GetHandle();
			int32 ReturnCode = INDEX_NONE;

			// This loop is the same than FCommandletLogPump::Run(), and it's used when SLocalizationCommandletExecutor widget runs localization commandlet tasks
			for (;;)
			{
				// Read from pipe.
				const FString PipeString = FPlatformProcess::ReadPipe(CommandletProcess->GetReadPipe());

				// Process buffer.
				if (!PipeString.IsEmpty())
				{
					UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("%s"), *PipeString);
				}

				// If the process isn't running and there's no data in the pipe, we're done.
				if (!FPlatformProcess::IsProcRunning(CurrentProcessHandle) && PipeString.IsEmpty())
				{
					break;
				}

				// Sleep.
				FPlatformProcess::Sleep(0.0f);
			}

			if (CurrentProcessHandle.IsValid() && FPlatformProcess::GetProcReturnCode(CurrentProcessHandle, &ReturnCode))
			{
				UE_LOG(LogGridlyImportExportCommandlet, Log, TEXT("===> Task [%s] returned : %d"), *LocTask.Name.ToString(), ReturnCode);
			}
		}
		else
		{
			UE_LOG(LogGridlyImportExportCommandlet, Warning, TEXT("Failed to start Task [%s] !"), *LocTask.Name.ToString());
		}
	}
}

#undef LOCTEXT_NAMESPACE