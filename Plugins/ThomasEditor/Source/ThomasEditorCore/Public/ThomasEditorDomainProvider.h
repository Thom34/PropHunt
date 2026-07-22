#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorDomainProviderModule : public IModuleInterface
{
public:
    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        int32 MaxResults) = 0;
};
