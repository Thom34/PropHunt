#include "Toolsets/ThomasDiscoveryToolset.h"

#include "ThomasEditorCoreModule.h"

FThomasCoreStatus UThomasDiscoveryToolset::GetStatus()
{
    return IThomasEditorCoreModule::Get().GetStatus();
}
