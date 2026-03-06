#include "MonolithSettings.h"

UMonolithSettings::UMonolithSettings()
{
}

const UMonolithSettings* UMonolithSettings::Get()
{
	return GetDefault<UMonolithSettings>();
}
