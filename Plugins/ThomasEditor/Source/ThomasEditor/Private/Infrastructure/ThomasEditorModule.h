#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlSettings.h"

class FThomasEditorModule final : public IModuleInterface, public FOutputDevice
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;

    static FThomasEditorModule& Get();

    bool IsAuthorized(const FString& Token) const;
    FString GetRecentMessagesJson(int32 Limit, const FString& Filter) const;

private:
    struct FBufferedMessage
    {
        FString Level;
        FString Category;
        FString Text;
    };

    void RegisterRemoteFunctions();
    void UnregisterRemoteFunctions();
    void ConfigureRemoteSecurity();
    void RestoreRemoteSecurity();
    void CreateSessionToken();
    void DeleteSessionToken();

    struct FRemoteSecuritySnapshot
    {
        bool bAllowAnyRemoteFunctionCall = false;
        bool bRestrictServerAccess = false;
        bool bEnableRemotePythonExecution = false;
        bool bAllowConsoleCommandRemoteExecution = false;
        bool bEnforcePassphraseForRemoteClients = true;
        FString AllowedOrigin;
        TSet<FRCNetworkAddressRange> AllowlistedClients;
    };

    FString SessionToken;
    FString SessionTokenPath;
    TOptional<FRemoteSecuritySnapshot> RemoteSecuritySnapshot;
    mutable FCriticalSection MessageMutex;
    TArray<FBufferedMessage> Messages;
};
