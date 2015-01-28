# - Try to find uuid
# Once done, this will define
#
#  uuid_FOUND - system has uuid
#  uuid_INCLUDE_DIRS - the uuid include directories
#  uuid_LIBRARIES - link these to use uuid

include(LibFindMacros)

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(uuid_PKGCONF uuid)

# Include dir
find_path(uuid_INCLUDE_DIR
  NAMES uuid/uuid.h
  PATHS ${uuid_PKGCONF_INCLUDE_DIRS}
)

# Finally the library itself
find_library(uuid_LIBRARY
  NAMES uuid
  PATHS ${uuid_PKGCONF_LIBRARY_DIRS}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries this this lib depends on.
set(uuid_PROCESS_INCLUDES uuid_INCLUDE_DIR)
set(uuid_PROCESS_LIBS uuid_LIBRARY)
libfind_process(uuid)
