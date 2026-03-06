#include "MonolithIndexNotification.h"

void FMonolithIndexNotification::Start()
{
	check(IsInGameThread());

	FNotificationInfo Info(FText::FromString(TEXT("Monolith: Indexing project...")));
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 0.0f;
	Info.FadeOutDuration = 1.0f;

	NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (auto Pinned = NotificationItem.Pin())
	{
		Pinned->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FMonolithIndexNotification::UpdateProgress(int32 Current, int32 Total)
{
	if (!IsInGameThread()) return;

	if (auto Pinned = NotificationItem.Pin())
	{
		float Pct = Total > 0 ? (static_cast<float>(Current) / static_cast<float>(Total)) * 100.0f : 0.0f;
		FText ProgressText = FText::FromString(
			FString::Printf(TEXT("Monolith: Indexing %d / %d assets (%.0f%%)"), Current, Total, Pct));
		Pinned->SetText(ProgressText);
	}
}

void FMonolithIndexNotification::Finish(bool bSuccess)
{
	if (!IsInGameThread()) return;

	if (auto Pinned = NotificationItem.Pin())
	{
		if (bSuccess)
		{
			Pinned->SetText(FText::FromString(TEXT("Monolith: Project indexing complete")));
			Pinned->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			Pinned->SetText(FText::FromString(TEXT("Monolith: Project indexing failed")));
			Pinned->SetCompletionState(SNotificationItem::CS_Fail);
		}
		Pinned->ExpireAndFadeout();
	}
}
