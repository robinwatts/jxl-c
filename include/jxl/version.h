#ifndef JXL_VERSION_H_
#define JXL_VERSION_H_

#define JXL_C_MAJOR_VERSION 0
#define JXL_C_MINOR_VERSION 12
#define JXL_C_PATCH_VERSION 0

#define JXL_C_COMPUTE_NUMERIC_VERSION(major, minor, patch) \
  (((major) << 24) | ((minor) << 16) | ((patch) << 8) | 0)

#define JXL_C_NUMERIC_VERSION \
  JXL_C_COMPUTE_NUMERIC_VERSION(JXL_C_MAJOR_VERSION, JXL_C_MINOR_VERSION, \
                                JXL_C_PATCH_VERSION)

#endif /* JXL_VERSION_H_ */
