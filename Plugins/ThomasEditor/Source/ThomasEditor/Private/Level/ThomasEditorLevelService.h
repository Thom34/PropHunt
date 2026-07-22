#pragma once

#include "CoreMinimal.h"

class FThomasEditorLevelService
{
public:
    static FString BuildAuditJson(
        const FString& MapPath,
        const FString& ClassFiltersJson,
        const FString& FoldersJson,
        const FString& RequiredFlagsJson,
        bool bIncludeActors,
        int32 MaxResults);
};
