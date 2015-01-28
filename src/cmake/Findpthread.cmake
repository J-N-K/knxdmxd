# - Try to find pthread
# Once done, this will define
#
#  pthread_FOUND - system has pthread
#  pthread_INCLUDE_DIRS - the pthread include directories
#  pthread_LIBRARIES - link these to use pthread

include(LibFindMacros)

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(pthread_PKGCONF pthread)

# Include dir
find_path(pthread_INCLUDE_DIR
  NAMES pthread.h
  PATHS ${pthread_PKGCONF_INCLUDE_DIRS}
)

# Finally the library itself
find_library(pthread_LIBRARY
  NAMES pthread
  PATHS ${pthread_PKGCONF_LIBRARY_DIRS}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries this this lib depends on.
set(pthread_PROCESS_INCLUDES pthread_INCLUDE_DIR)
set(pthread_PROCESS_LIBS pthread_LIBRARY)
libfind_process(pthread)
