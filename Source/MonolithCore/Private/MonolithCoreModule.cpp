#include "MonolithCoreModule.h"
#include "MonolithHttpServer.h"
#include "MonolithSettings.h"
#include "MonolithJsonUtils.h"
#include "MonolithToolRegistry.h"
#include "MonolithCoreTools.h"

#define LOCTEXT_NAMESPACE "FMonolithCoreModule"

void FMonolithCoreModule::StartupModule()
{
	UE_LOG(LogMonolith, Log, TEXT("Monolith %s — Core module initializing"), MONOLITH_VERSION);

	// Register core discovery/status tools
	RegisterCoreTools();

	// Start HTTP server
	const UMonolithSettings* Settings = UMonolithSettings::Get();
	int32 Port = Settings ? Settings->ServerPort : 9316;

	HttpServer = MakeUnique<FMonolithHttpServer>();
	if (!HttpServer->Start(Port))
	{
		UE_LOG(LogMonolith, Error, TEXT("Failed to start MCP server on port %d"), Port);
	}
}

void FMonolithCoreModule::ShutdownModule()
{
	if (HttpServer.IsValid())
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}

	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("monolith"));

	UE_LOG(LogMonolith, Log, TEXT("Monolith — Core module shut down"));
}

void FMonolithCoreModule::RegisterCoreTools()
{
	FMonolithCoreTools::RegisterAll();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithCoreModule, MonolithCore)
