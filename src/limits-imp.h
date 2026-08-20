#ifndef JXL_LIMITS_H

#define JXL_LIMITS_H

#include <limits.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

/* Define SSIZE_MAX / SSIZE_MIN if missing */
#ifndef SSIZE_MAX
#ifdef _WIN64
#define SSIZE_MAX _I64_MAX
#define SSIZE_MIN _I64_MIN
#else
#define SSIZE_MAX _I32_MAX
#define SSIZE_MIN _I32_MIN
#endif
#endif

#endif
