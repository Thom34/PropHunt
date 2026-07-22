#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorValidationProviderModule : public IModuleInterface
{
public:
    virtual FThomasValidationResult ValidateAssets(
        const TArray<FString>& AssetPaths,
        int32 MaxMessages) = 0;
    virtual FThomasValidationResult RunCurrentMapCheck(int32 MaxMessages) = 0;
};
