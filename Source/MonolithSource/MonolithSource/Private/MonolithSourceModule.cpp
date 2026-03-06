#include "MonolithSourceModule.h"
#include "MonolithSourceActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"

#define LOCTEXT_NAMESPACE "FMonolithSourceModule"

void FMonolithSourceModule::StartupModule()
{
	FMonolithSourceActions::RegisterAll();
	UE_LOG(LogMonolith, Log, TEXT("Monolith \u2014 Source module loaded (10 actions, bundled Python indexer)"));
}

void FMonolithSourceModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("source"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithSourceModule, MonolithSource)
