cmake_minimum_required (VERSION 2.6)

#------------------------------------------------------------
# project name/version
#------------------------------------------------------------

project (mpiglue)
set (MPIGLUE_VERSION 0.1)

#------------------------------------------------------------
# compiler settings
#------------------------------------------------------------

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)

include_directories(
	"${PROJECT_SOURCE_DIR}"
	"${PROJECT_BINARY_DIR}"
)

#------------------------------------------------------------
# header with compile time settings
#------------------------------------------------------------

configure_file(
	"${PROJECT_SOURCE_DIR}/config.h.in"
	"${PROJECT_BINARY_DIR}/config.h"
)

#------------------------------------------------------------
# main executable
#------------------------------------------------------------

add_executable (mpi mpi.cc)

#------------------------------------------------------------
# install targets
#------------------------------------------------------------

install (TARGETS mpi DESTINATION bin)
