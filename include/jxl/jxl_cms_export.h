#ifndef JXL_CMS_EXPORT_H
#define JXL_CMS_EXPORT_H

#ifdef JXL_CMS_STATIC_DEFINE
#  define JXL_CMS_EXPORT
#  define JXL_CMS_NO_EXPORT
#else
#  ifndef JXL_CMS_EXPORT
#    define JXL_CMS_EXPORT
#  endif
#  ifndef JXL_CMS_NO_EXPORT
#    define JXL_CMS_NO_EXPORT
#  endif
#endif

#endif /* JXL_CMS_EXPORT_H */
