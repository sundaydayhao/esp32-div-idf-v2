#pragma once

#if defined(CONFIG_BT_BLUEDROID_ENABLED) && !defined(CONFIG_BLUEDROID_ENABLED)
#define CONFIG_BLUEDROID_ENABLED 1
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED) && !defined(CONFIG_NIMBLE_ENABLED)
#define CONFIG_NIMBLE_ENABLED 1
#endif

#include <nimble_device.h>
