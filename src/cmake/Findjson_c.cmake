# - Try to find json-c
# Once done, this will define
#
#  json_c_FOUND - system has json-c
#  json_c_INCLUDE_DIRS - the json-c include directories
#  json_c_LIBRARIES - link these to use json-c

include(LibFindMacros)

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(json_c_PKGCONF json_c)

# Include dir
find_path(json_c_INCLUDE_DIR
  NAMES 
    json/json.h
  PATHS 
    ${json_c_PKGCONF_INCLUDE_DIRS}
    /usr/local/include
)

# Finally the library itself
find_library(json_c_LIBRARY
  NAMES json
  PATHS ${json_c_PKGCONF_LIBRARY_DIRS}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries this this lib depends on.
set(json_c_PROCESS_INCLUDE json_c_INCLUDE_DIR)
set(json_c_PROCESS_LIB json_c_LIBRARY)
libfind_process(json_c)
