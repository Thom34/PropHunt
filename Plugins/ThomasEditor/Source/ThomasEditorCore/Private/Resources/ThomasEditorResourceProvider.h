#pragma once

#include "IModelContextProtocolResourceProvider.h"

/** Small read-only context surface that never loads a domain provider. */
class FThomasEditorResourceProvider final : public IModelContextProtocolResourceProvider
{
public:
    virtual void ListResources(
        FModelContextProtocolResourceDescriptorList& OutResourceDescriptors) const override;

    virtual TValueOrError<FModelContextProtocolResource, FString> ReadResource(
        const FString& Uri) const override;
};
