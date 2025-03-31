// Copyright (c) 2021 LocalizeDirect AB

#include "GridlyGameSettings.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

UGridlyGameSettings::UGridlyGameSettings(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer),
    CustomCultureMapping({
        {"en-US", "enUS"},
        {"ar-SA", "arSA"},
        {"ca-ES", "caES"},
        {"zh-CN", "zhCN"},
        {"zh-TW", "zhTW"},
        {"de-DE", "deDE"},
        {"it-IT", "itIT"},
        {"ja-JP", "jaJP"},
        {"ko-KR", "koKR"},
        {"pl-PL", "plPL"},
        {"pt-BR", "ptBR"},
        {"ru-RU", "ruRU"},
        {"es-MX", "esMX"},
        {"es-ES", "esES"},
        {"bn-BD", "bnBD"},
        {"bg-BG", "bgBG"},
        {"zh-HK", "zhHK"},
        {"cs-CZ", "csCZ"},
        {"da-DK", "daDK"},
        {"nl-NL", "nlNL"},
        {"fi-FI", "fiFI"},
        {"fr-CA", "frCA"},
        {"fr-FR", "frFR"},
        {"el-GR", "elGR"},
        {"he-IL", "heIL"},
        {"hi-IN", "hiIN"},
        {"hu-HU", "huHU"},
        {"id-ID", "idID"},
        {"jw-ID", "jwID"},
        {"lv-LV", "lvLV"},
        {"ms-MY", "msMY"},
        {"no-NO", "noNO"},
        {"pt-PT", "ptPT"},
        {"ro-RO", "roRO"},
        {"sk-SK", "skSK"},
        {"sv-SE", "svSE"},
        {"tl-PH", "tlPH"},
        {"th-TH", "thTH"},
        {"tr-TR", "trTR"},
        {"uk-UA", "ukUA"},
        {"ur-IN", "urIN"},
        {"vi-VN", "viVN"},
        {"af-ZA", "afZA"},
        {"ar-AE", "arAE"},
        {"ar-BH", "arBH"},
        {"ar-DZ", "arDZ"},
        {"ar-EG", "arEG"},
        {"ar-IQ", "arIQ"},
        {"ar-JO", "arJO"},
        {"ar-KW", "arKW"},
        {"ar-LB", "arLB"},
        {"ar-LY", "arLY"},
        {"ar-MA", "arMA"},
        {"ar-OM", "arOM"},
        {"ar-QA", "arQA"},
        {"ar-SY", "arSY"},
        {"ar-TN", "arTN"},
        {"ar-YE", "arYE"},
        {"az-AZ", "azAZ"},
        {"be-BY", "beBY"},
        {"bs-BA", "bsBA"},
        {"cy-GB", "cyGB"},
        {"de-AT", "deAT"},
        {"de-CH", "deCH"},
        {"de-LI", "deLI"},
        {"de-LU", "deLU"},
        {"dv-MV", "dvMV"},
        {"en-AU", "enAU"},
        {"en-BZ", "enBZ"},
        {"en-CA", "enCA"},
        {"en-GB", "enGB"},
        {"en-IE", "enIE"},
        {"en-JM", "enJM"},
        {"en-NZ", "enNZ"},
        {"en-PH", "enPH"},
        {"en-TT", "enTT"},
        {"en-ZA", "enZA"},
        {"en-ZW", "enZW"},
        {"es-AR", "esAR"},
        {"es-BO", "esBO"},
        {"es-CL", "esCL"},
        {"es-CO", "esCO"},
        {"es-CR", "esCR"},
        {"es-DO", "esDO"},
        {"es-EC", "esEC"},
        {"es-GT", "esGT"},
        {"es-HN", "esHN"},
        {"es-NI", "esNI"},
        {"es-PA", "esPA"},
        {"es-PE", "esPE"},
        {"es-PR", "esPR"},
        {"es-PY", "esPY"},
        {"es-SV", "esSV"},
        {"es-UY", "esUY"},
        {"es-VE", "esVE"},
        {"et-EE", "etEE"},
        {"eu-ES", "euES"},
        {"fa-IR", "faIR"},
        {"fo-FO", "foFO"},
        {"fr-BE", "frBE"},
        {"fr-CH", "frCH"},
        {"fr-LU", "frLU"},
        {"fr-MC", "frMC"},
        {"gl-ES", "glES"},
        {"gu-IN", "guIN"},
        {"hr-BA", "hrBA"},
        {"hr-HR", "hrHR"},
        {"hy-AM", "hyAM"},
        {"is-IS", "isIS"},
        {"it-CH", "itCH"},
        {"ka-GE", "kaGE"},
        {"kk-KZ", "kkKZ"},
        {"kn-IN", "knIN"},
        {"kok-IN", "kokIN"},
        {"ky-KG", "kyKG"},
        {"lt-LT", "ltLT"},
        {"mi-NZ", "miNZ"},
        {"mk-MK", "mkMK"},
        {"mn-MN", "mnMN"},
        {"mr-IN", "mrIN"},
        {"ms-BN", "msBN"},
        {"mt-MT", "mtMT"},
        {"nb-NO", "nbNO"},
        {"nl-BE", "nlBE"},
        {"nn-NO", "nnNO"},
        {"ns-ZA", "nsZA"},
        {"pa-IN", "paIN"},
        {"ps-AR", "psAR"},
        {"qu-BO", "quBO"},
        {"qu-EC", "quEC"},
        {"qu-PE", "quPE"},
        {"sa-IN", "saIN"},
        {"se-FI", "seFI"},
        {"se-NO", "seNO"},
        {"se-SE", "seSE"},
        {"sl-SI", "slSI"},
        {"sq-AL", "sqAL"},
        {"sr-BA", "srBA"},
        {"sv-FI", "svFI"},
        {"sw-KE", "swKE"},
        {"syr-SY", "syrSY"},
        {"ta-IN", "taIN"},
        {"te-IN", "teIN"},
        {"tn-ZA", "tnZA"},
        {"tt-RU", "ttRU"},
        {"ur-PK", "urPK"},
        {"uz-UZ", "uzUZ"},
        {"xh-ZA", "xhZA"},
        {"zh-MO", "zhMO"},
        {"zh-SG", "zhSG"},
        {"zu-ZA", "zuZA"}
        })
{
#if WITH_EDITOR
    FString GridlyConfigPath = GetGridlyConfigPath();

    // Ensure the config file exists
    EnsureConfigFileExists(GridlyConfigPath);

    // Try to read settings from the custom configuration file first
    if (!GConfig->GetString(
        TEXT("Gridly"),
        TEXT("GridlyExportApiKey"),
        ExportApiKey,
        GridlyConfigPath))
    {
        // If not found, read from the project-wide configuration file
        GConfig->GetString(
            TEXT("/Script/Gridly.GridlyGameSettings"), // Replace with your actual module name
            TEXT("ExportApiKey"),
            ExportApiKey,
            GGameIni
        );
    }

    if (!GConfig->GetString(
        TEXT("Gridly"),
        TEXT("GridlyExportViewId"),
        ExportViewId,
        GridlyConfigPath))
    {
        // If not found, read from the project-wide configuration file
        GConfig->GetString(
            TEXT("/Script/Gridly.GridlyGameSettings"), // Replace with your actual module name
            TEXT("ExportViewId"),
            ExportViewId,
            GGameIni
        );
    }

    FString ImportFromViewIdsJson;
    if (!GConfig->GetString(
        TEXT("Gridly"),
        TEXT("GridlyImportFromViewIds"),
        ImportFromViewIdsJson,
        GridlyConfigPath))
    {
        // If not found, read from the project-wide configuration file
        GConfig->GetString(
            TEXT("/Script/Gridly.GridlyGameSettings"), // Replace with your actual module name
            TEXT("ImportFromViewIds"),
            ImportFromViewIdsJson,
            GGameIni
        );
    }

    if (!GConfig->GetString(
        TEXT("Gridly"),
        TEXT("GridlyImportApiKey"),
        ImportApiKey,
        GridlyConfigPath))
    {
        // If not found, read from the project-wide configuration file
        GConfig->GetString(
            TEXT("/Script/Gridly.GridlyGameSettings"), // Replace with your actual module name
            TEXT("ImportApiKey"),
            ImportApiKey,
            GGameIni
        );
    }

    // Deserialize JSON string to ImportFromViewIds array
    DeserializeJsonToArray(ImportFromViewIdsJson, ImportFromViewIds);
    bUseCombinedNamespaceId = false;

#endif
}

bool UGridlyGameSettings::OnSettingsSaved()
{
#if WITH_EDITOR
    UGridlyGameSettings* Settings = GetMutableDefault<UGridlyGameSettings>();
    FString ConfigPath = GetGridlyConfigPath();

    Settings->Modify();  // Required for editor changes

    // Make sure config is loaded into memory!
    GConfig->LoadFile(ConfigPath);
    EnsureConfigFileExists(ConfigPath);

    // Write to config
    GConfig->SetString(TEXT("Gridly"), TEXT("GridlyExportApiKey"), *Settings->ExportApiKey, ConfigPath);
    GConfig->SetString(TEXT("Gridly"), TEXT("GridlyExportViewId"), *Settings->ExportViewId, ConfigPath);
    GConfig->SetString(TEXT("Gridly"), TEXT("GridlyImportApiKey"), *Settings->ImportApiKey, ConfigPath);

    FString ImportFromViewIdsJson = SerializeArrayToJson(Settings->ImportFromViewIds);
    GConfig->SetString(TEXT("Gridly"), TEXT("GridlyImportFromViewIds"), *ImportFromViewIdsJson, ConfigPath);

    // Save to disk
    GConfig->Flush(false, ConfigPath);

    UE_LOG(LogTemp, Log, TEXT("✅ Gridly settings saved to: %s"), *ConfigPath);
#endif
    return true;
}

// Serialize an array to a JSON string
FString UGridlyGameSettings::SerializeArrayToJson(const TArray<FString>& Array)
{
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);

    Writer->WriteArrayStart();
    for (const FString& Item : Array)
    {
        Writer->WriteValue(Item);
    }
    Writer->WriteArrayEnd();

    Writer->Close();
    return OutputString;
}

bool UGridlyGameSettings::DeserializeJsonToArray(const FString& JsonString, TArray<FString>& OutArray)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    TArray<TSharedPtr<FJsonValue>> JsonArray;

    if (FJsonSerializer::Deserialize(Reader, JsonArray))
    {
        OutArray.Empty();  // Clear existing content
        for (const TSharedPtr<FJsonValue>& Value : JsonArray)
        {
            OutArray.Add(Value->AsString());
        }
        return true;
    }
    return false;
}

FString UGridlyGameSettings::GetGridlyConfigPath()
{
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()) / TEXT("GridlyConfig.ini");
    FConfigCacheIni::NormalizeConfigIniPath(Path); // Normalize it!
    return Path;
}

void UGridlyGameSettings::EnsureConfigFileExists(const FString& ConfigPath)
{
    if (!FPaths::FileExists(ConfigPath))
    {
        FString DefaultContent =
            TEXT("[Gridly]\n")
            TEXT("GridlyExportApiKey=\n")
            TEXT("GridlyExportViewId=\n")
            TEXT("GridlyImportApiKey=\n")
            TEXT("GridlyImportFromViewIds=[]\n")
            TEXT("CustomCultureMapping=(\"en-US\", \"enUS\"),(\"ar-SA\", \"arSA\"),(\"ca-ES\", \"caES\"),(\"zh-CN\", \"zhCN\"),(\"zh-TW\", \"zhTW\"),(\"de-DE\", \"deDE\"),(\"it-IT\", \"itIT\"),(\"ja-JP\", \"jaJP\"),(\"ko-KR\", \"koKR\"),(\"pl-PL\", \"plPL\"),(\"pt-BR\", \"ptBR\"),(\"ru-RU\", \"ruRU\"),(\"es-MX\", \"esMX\"),(\"es-ES\", \"esES\"),(\"bn-BD\", \"bnBD\"),(\"bg-BG\", \"bgBG\"),(\"zh-HK\", \"zhHK\"),(\"cs-CZ\", \"csCZ\"),(\"da-DK\", \"daDK\"),(\"nl-NL\", \"nlNL\"),(\"fi-FI\", \"fiFI\"),(\"fr-CA\", \"frCA\"),(\"fr-FR\", \"frFR\"),(\"el-GR\", \"elGR\"),(\"he-IL\", \"heIL\"),(\"hi-IN\", \"hiIN\"),(\"hu-HU\", \"huHU\"),(\"id-ID\", \"idID\"),(\"jw-ID\", \"jwID\"),(\"lv-LV\", \"lvLV\"),(\"ms-MY\", \"msMY\"),(\"no-NO\", \"noNO\"),(\"pt-PT\", \"ptPT\"),(\"ro-RO\", \"roRO\"),(\"sk-SK\", \"skSK\"),(\"sv-SE\", \"svSE\"),(\"tl-PH\", \"tlPH\"),(\"th-TH\", \"thTH\"),(\"tr-TR\", \"trTR\"),(\"uk-UA\", \"ukUA\"),(\"ur-IN\", \"urIN\"),(\"vi-VN\", \"viVN\"),(\"af-ZA\", \"afZA\"),(\"ar-AE\", \"arAE\"),(\"ar-BH\", \"arBH\"),(\"ar-DZ\", \"arDZ\"),(\"ar-EG\", \"arEG\"),(\"ar-IQ\", \"arIQ\"),(\"ar-JO\", \"arJO\"),(\"ar-KW\", \"arKW\"),(\"ar-LB\", \"arLB\"),(\"ar-LY\", \"arLY\"),(\"ar-MA\", \"arMA\"),(\"ar-OM\", \"arOM\"),(\"ar-QA\", \"arQA\"),(\"ar-SY\", \"arSY\"),(\"ar-TN\", \"arTN\"),(\"ar-YE\", \"arYE\"),(\"az-AZ\", \"azAZ\"),(\"be-BY\", \"beBY\"),(\"bs-BA\", \"bsBA\"),(\"cy-GB\", \"cyGB\"),(\"de-AT\", \"deAT\"),(\"de-CH\", \"deCH\"),(\"de-LI\", \"deLI\"),(\"de-LU\", \"deLU\"),(\"dv-MV\", \"dvMV\"),(\"en-AU\", \"enAU\"),(\"en-BZ\", \"enBZ\"),(\"en-CA\", \"enCA\"),(\"en-GB\", \"enGB\"),(\"en-IE\", \"enIE\"),(\"en-JM\", \"enJM\"),(\"en-NZ\", \"enNZ\"),(\"en-PH\", \"enPH\"),(\"en-TT\", \"enTT\"),(\"en-ZA\", \"enZA\"),(\"en-ZW\", \"enZW\"),(\"es-AR\", \"esAR\"),(\"es-BO\", \"esBO\"),(\"es-CL\", \"esCL\"),(\"es-CO\", \"esCO\"),(\"es-CR\", \"esCR\"),(\"es-DO\", \"esDO\"),(\"es-EC\", \"esEC\"),(\"es-GT\", \"esGT\"),(\"es-HN\", \"esHN\"),(\"es-NI\", \"esNI\"),(\"es-PA\", \"esPA\"),(\"es-PE\", \"esPE\"),(\"es-PR\", \"esPR\"),(\"es-PY\", \"esPY\"),(\"es-SV\", \"esSV\"),(\"es-UY\", \"esUY\"),(\"es-VE\", \"esVE\"),(\"et-EE\", \"etEE\"),(\"eu-ES\", \"euES\"),(\"fa-IR\", \"faIR\"),(\"fo-FO\", \"foFO\"),(\"fr-BE\", \"frBE\"),(\"fr-CH\", \"frCH\"),(\"fr-LU\", \"frLU\"),(\"fr-MC\", \"frMC\"),(\"gl-ES\", \"glES\"),(\"gu-IN\", \"guIN\"),(\"hr-BA\", \"hrBA\"),(\"hr-HR\", \"hrHR\"),(\"hy-AM\", \"hyAM\"),(\"is-IS\", \"isIS\"),(\"it-CH\", \"itCH\"),(\"ka-GE\", \"kaGE\"),(\"kk-KZ\", \"kkKZ\"),(\"kn-IN\", \"knIN\"),(\"kok-IN\", \"kokIN\"),(\"ky-KG\", \"kyKG\"),(\"lt-LT\", \"ltLT\"),(\"mi-NZ\", \"miNZ\"),(\"mk-MK\", \"mkMK\"),(\"mn-MN\", \"mnMN\"),(\"mr-IN\", \"mrIN\"),(\"ms-BN\", \"msBN\"),(\"mt-MT\", \"mtMT\"),(\"nb-NO\", \"nbNO\"),(\"nl-BE\", \"nlBE\"),(\"nn-NO\", \"nnNO\"),(\"ns-ZA\", \"nsZA\"),(\"pa-IN\", \"paIN\"),(\"ps-AR\", \"psAR\"),(\"qu-BO\", \"quBO\"),(\"qu-EC\", \"quEC\"),(\"qu-PE\", \"quPE\"),(\"sa-IN\", \"saIN\"),(\"se-FI\", \"seFI\"),(\"se-NO\", \"seNO\"),(\"se-SE\", \"seSE\"),(\"sl-SI\", \"slSI\"),(\"sq-AL\", \"sqAL\"),(\"sr-BA\", \"srBA\"),(\"sv-FI\", \"svFI\"),(\"sw-KE\", \"swKE\"),(\"syr-SY\", \"syrSY\"),(\"ta-IN\", \"taIN\"),(\"te-IN\", \"teIN\"),(\"tn-ZA\", \"tnZA\"),(\"tt-RU\", \"ttRU\"),(\"ur-PK\", \"urPK\"),(\"uz-UZ\", \"uzUZ\"),(\"xh-ZA\", \"xhZA\"),(\"zh-MO\", \"zhMO\"),(\"zh-SG\", \"zhSG\"),(\"zu-ZA\", \"zuZA\"),(\"en\", \"en\")\n");

        FFileHelper::SaveStringToFile(DefaultContent, *ConfigPath);
    }
}



