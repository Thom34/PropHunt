#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorCoreModule : public IModuleInterface
{
public:
    static IThomasEditorCoreModule& Get()
    {
        return FModuleManager::LoadModuleChecked<IThomasEditorCoreModule>(TEXT("ThomasEditorCore"));
    }

    virtual FThomasCoreStatus GetStatus() const = 0;
    virtual FString GetRecentMessagesJson(int32 Limit, const FString& Filter) const = 0;
};
