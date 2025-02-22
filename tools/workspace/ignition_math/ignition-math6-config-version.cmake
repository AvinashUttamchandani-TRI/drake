# TODO(jwnimmer-tri) On 2021-12-01 when the ":ignition_math" target is removed,
# we should also remove this file (and its mention in BUILD.bazel).

set(PACKAGE_VERSION "6.8.0")
set(PACKAGE_COMPAT_VERSION "6.8.0")

if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
elseif(PACKAGE_VERSION VERSION_LESS COMPAT_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
  if(PACKAGE_VERSION STREQUAL PACKAGE_FIND_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
  endif()
endif()

unset(PACKAGE_COMPAT_VERSION)

