#pragma once

#include <3ds/types.h>

Result initializeReslimits(void);
Result setAppCpuTimeLimit(s64 limit);
Result getAppCpuTimeLimit(s64 *limit);

Result SetAppResourceLimit(u32 mbz, ResourceLimitType category, u32 value, u64 mbz2);
Result GetAppResourceLimit(s64 *value, u32 mbz, ResourceLimitType category, u32 mbz2, u64 mbz3);
