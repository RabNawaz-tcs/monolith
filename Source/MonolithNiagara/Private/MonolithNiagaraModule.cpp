#include "MonolithNiagaraModule.h"
#include "MonolithNiagaraActions.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithNiagaraModule"

void FMonolithNiagaraModule::StartupModule()
{
	FMonolithNiagaraActions::RegisterActions(FMonolithToolRegistry::Get());
	UE_LOG(LogTemp, Log, TEXT("Monolith — Niagara module loaded (39 actions)"));
}

void FMonolithNiagaraModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("niagara"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithNiagaraModule, MonolithNiagara)
