# - Try to find eibclient
# Once done, this will define
#
#  eibclient_FOUND - system has eibclient
#  eibclient_INCLUDE_DIRS - the eibclient include directories
#  eibclient_LIBRARIES - link these to use eibclient

include(LibFindMacros)

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(eibclient_PKGCONF eibclient)

# Include dir
find_path(eibclient_INCLUDE_DIR
  NAMES eibclient.h
  PATHS ${eibclient_PKGCONF_INCLUDE_DIRS}
)

# Finally the library itself
find_library(eibclient_LIBRARY
  NAMES eibclient
  PATHS ${eibclient_PKGCONF_LIBRARY_DIRS}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries this this lib depends on.
set(eibclient_PROCESS_INCLUDES eibclient_INCLUDE_DIR)
set(eibclient_PROCESS_LIBS eibclient_LIBRARY)
libfind_process(eibclient)
