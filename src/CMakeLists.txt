cmake_minimum_required (VERSION 2.6)
project(knxdmxd)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
find_package(json_c REQUIRED)
find_package(eibclient REQUIRED)
find_package(pthread REQUIRED)
find_package(uuid REQUIRED)

include_directories(
  ${CMAKE_SOURCE_DIR} 
  ${json_c_INCLUDE_DIR} 
  ${eibclient_INCLUDE_DIR}
  ${pthread_INCLUDE_DIR}
  ${uuid_INCLUDE_DIR}
)

set(LIBS 
  ${LIBS} 
  ${json_c_LIBRARY} 
  ${eibclient_LIBRARY}
  ${pthread_LIBRARY}
  ${uuid_LIBRARY}
)

# target
add_executable(knxdmxd knxdmxd.c)

target_link_libraries(knxdmxd ${LIBS})

## Compiler flags
if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_C_FLAGS "-g -O2 -Wall")        ## Optimize
    set(CMAKE_EXE_LINKER_FLAGS "-s")  ## Strip binary
endif()

