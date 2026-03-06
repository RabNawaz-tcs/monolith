#include "MonolithMaterialModule.h"
#include "MonolithMaterialActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"

#define LOCTEXT_NAMESPACE "FMonolithMaterialModule"

void FMonolithMaterialModule::StartupModule()
{
	FMonolithMaterialActions::RegisterActions(FMonolithToolRegistry::Get());
	UE_LOG(LogMonolith, Log, TEXT("Monolith — Material module loaded (14 actions)"));
}

void FMonolithMaterialModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("material"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithMaterialModule, MonolithMaterial)
