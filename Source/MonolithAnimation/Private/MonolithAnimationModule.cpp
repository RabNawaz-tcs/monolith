#include "MonolithAnimationModule.h"
#include "MonolithAnimationActions.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithAnimationModule"

void FMonolithAnimationModule::StartupModule()
{
	FMonolithAnimationActions::RegisterActions(FMonolithToolRegistry::Get());
	UE_LOG(LogTemp, Log, TEXT("Monolith — Animation module loaded (23 actions)"));
}

void FMonolithAnimationModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("animation"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithAnimationModule, MonolithAnimation)
