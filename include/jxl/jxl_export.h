#ifndef JXL_EXPORT_H
#define JXL_EXPORT_H

#ifdef JXL_C_STATIC_DEFINE
#  define JXL_C_EXPORT
#  define JXL_C_NO_EXPORT
#else
#  ifndef JXL_C_EXPORT
#    define JXL_C_EXPORT
#  endif
#  ifndef JXL_C_NO_EXPORT
#    define JXL_C_NO_EXPORT
#  endif
#endif

#ifndef JXL_C_DEPRECATED
#  define JXL_C_DEPRECATED
#endif

#endif /* JXL_EXPORT_H */
