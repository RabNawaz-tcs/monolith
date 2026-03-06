#include "MonolithEditorModule.h"
#include "MonolithEditorActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"
#include "Misc/OutputDeviceRedirector.h"

#define LOCTEXT_NAMESPACE "FMonolithEditorModule"

void FMonolithEditorModule::StartupModule()
{
	LogCapture = new FMonolithLogCapture();
	GLog->AddOutputDevice(LogCapture);

	FMonolithEditorActions::RegisterActions(LogCapture);
	UE_LOG(LogMonolith, Log, TEXT("Monolith — Editor module loaded (11 actions)"));
}

void FMonolithEditorModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("editor"));

	if (LogCapture)
	{
		GLog->RemoveOutputDevice(LogCapture);
		delete LogCapture;
		LogCapture = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithEditorModule, MonolithEditor)
