#include "MonolithSourceModule.h"
#include "MonolithJsonUtils.h"

#define LOCTEXT_NAMESPACE "FMonolithSourceModule"

void FMonolithSourceModule::StartupModule()
{
	UE_LOG(LogMonolith, Verbose, TEXT("Monolith — Source module loaded (14 actions, bundled Python indexer)"));
}

void FMonolithSourceModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithSourceModule, MonolithSource)
