#ifndef JXL_VERSION_H_
#define JXL_VERSION_H_

#define JXL_MAJOR_VERSION 0
#define JXL_MINOR_VERSION 12
#define JXL_PATCH_VERSION 0

#define JXL_COMPUTE_NUMERIC_VERSION(major, minor, patch) \
  (((major) << 24) | ((minor) << 16) | ((patch) << 8) | 0)

#define JXL_NUMERIC_VERSION \
  JXL_COMPUTE_NUMERIC_VERSION(JXL_MAJOR_VERSION, JXL_MINOR_VERSION, \
                              JXL_PATCH_VERSION)

#endif /* JXL_VERSION_H_ */
