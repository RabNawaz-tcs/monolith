#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"
#include "Misc/OutputDevice.h"

struct FMonolithLogEntry
{
	double Timestamp;
	FName Category;
	ELogVerbosity::Type Verbosity;
	FString Message;
};

class FMonolithLogCapture : public FOutputDevice
{
public:
	static constexpr int32 MaxEntries = 10000;

	void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

	TArray<FMonolithLogEntry> GetRecentEntries(int32 Count) const;
	TArray<FMonolithLogEntry> SearchEntries(const FString& Pattern, const FString& CategoryFilter, ELogVerbosity::Type MaxVerbosity, int32 Limit) const;
	TArray<FString> GetActiveCategories() const;

	int32 GetCountByVerbosity(ELogVerbosity::Type Verbosity) const;
	int32 GetTotalCount() const;

private:
	mutable FCriticalSection Lock;
	TArray<FMonolithLogEntry> RingBuffer;
	int32 WriteIndex = 0;
	bool bWrapped = false;

	int32 TotalFatal = 0;
	int32 TotalError = 0;
	int32 TotalWarning = 0;
	int32 TotalLog = 0;
	int32 TotalVerbose = 0;
};

class FMonolithEditorActions
{
public:
	static void RegisterActions(FMonolithLogCapture* LogCapture);

	static FMonolithActionResult HandleTriggerBuild(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetBuildErrors(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetBuildStatus(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetBuildSummary(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSearchBuildOutput(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetRecentLogs(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSearchLogs(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleTailLog(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetLogCategories(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetLogStats(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetCrashContext(const TSharedPtr<FJsonObject>& Params);

private:
	static FMonolithLogCapture* CachedLogCapture;
};
