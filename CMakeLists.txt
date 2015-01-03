cmake_minimum_required (VERSION 2.6)

#------------------------------------------------------------
# project name/version
#------------------------------------------------------------

project (mpih)
set (PROGRAM_NAME mpih)
set (PROGRAM_VERSION 0.1)

#------------------------------------------------------------
# dependencies
#------------------------------------------------------------

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# MPI

find_package(MPI REQUIRED)
SET(CMAKE_C_COMPILER mpicc)
SET(CMAKE_CXX_COMPILER mpicxx)

# libevent

find_package(Event)

#------------------------------------------------------------
# compiler settings
#------------------------------------------------------------

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
include_directories(
	"${PROJECT_SOURCE_DIR}"
	"${PROJECT_BINARY_DIR}"
	SYSTEM
		"${MPI_INCLUDE_PATH}"
		"${EVENT_INCLUDE_DIR}"
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

add_executable (mpih mpih.cc)
target_link_libraries(mpih "${MPI_C_LIBRARIES}" "${EVENT_LIBRARIES}")

#------------------------------------------------------------
# install targets
#------------------------------------------------------------

install (TARGETS mpih DESTINATION bin)
