#ifndef JXL_EXPORT_H
#define JXL_EXPORT_H

#ifdef JXL_STATIC_DEFINE
#  define JXL_EXPORT
#  define JXL_NO_EXPORT
#else
#  ifndef JXL_EXPORT
#    define JXL_EXPORT
#  endif
#  ifndef JXL_NO_EXPORT
#    define JXL_NO_EXPORT
#  endif
#endif

#ifndef JXL_DEPRECATED
#  define JXL_DEPRECATED
#endif

#endif /* JXL_EXPORT_H */
