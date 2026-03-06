#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

/**
 * Manages the Slate notification for indexing progress.
 * Shows a persistent notification with progress updates,
 * auto-dismisses on completion.
 */
class FMonolithIndexNotification
{
public:
	/** Show the indexing notification */
	void Start();

	/** Update progress (0.0 - 1.0) with current/total counts */
	void UpdateProgress(int32 Current, int32 Total);

	/** Mark indexing as complete */
	void Finish(bool bSuccess);

private:
	TWeakPtr<SNotificationItem> NotificationItem;
};
