#include "Toolsets/ThomasValidationToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorValidationProvider.h"

namespace
{
IThomasEditorValidationProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorValidationProviderModule>(
        TEXT("ThomasEditorValidation"));
}

FThomasValidationResult Unavailable()
{
    FThomasValidationResult Result;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorValidation could not be loaded.");
    return Result;
}
}

FThomasValidationResult UThomasValidationToolset::ValidateAssets(
    const TArray<FString>& AssetPaths,
    const int32 MaxMessages)
{
    if (IThomasEditorValidationProviderModule* Provider = LoadProvider())
    {
        return Provider->ValidateAssets(AssetPaths, MaxMessages);
    }
    return Unavailable();
}

FThomasValidationResult UThomasValidationToolset::RunCurrentMapCheck(
    const int32 MaxMessages)
{
    if (IThomasEditorValidationProviderModule* Provider = LoadProvider())
    {
        return Provider->RunCurrentMapCheck(MaxMessages);
    }
    return Unavailable();
}
