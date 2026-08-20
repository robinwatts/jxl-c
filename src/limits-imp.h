#ifndef JXL_LIMITS_H

#define JXL_LIMITS_H

#include <limits.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

/* Define SSIZE_MAX / SSIZE_MIN if missing */
#ifndef SSIZE_MAX
#ifdef _WIN64
#define SSIZE_MAX _I64_MAX
#define SSIZE_MIN _I64_MIN
#elif defined(_WIN32)
#define SSIZE_MAX _I32_MAX
#define SSIZE_MIN _I32_MIN
#elif defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 8
#define SSIZE_MAX 0x7fffffffffffffffL
#define SSIZE_MIN (-SSIZE_MAX - 1)
#else
#define SSIZE_MAX 0x7fffffffL
#define SSIZE_MIN (-SSIZE_MAX - 1)
#endif
#endif

#endif
