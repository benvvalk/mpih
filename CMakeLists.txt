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

# Google PerfTools (optional)

find_package(Gperftools)

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

set(SOURCE_FILES
Command/client/event_handlers.h
Command/commands.h
Command/finalize.h
Command/help.h
Command/init/Connection.h
Command/init/event_handlers.h
Command/init.h
Command/init/log.h
Command/init/MPIChannel.h
Command/init/mpi.h
Command/rank.h
Command/recv.h
Command/run.h
Command/send.h
Command/size.h
Command/version.h
Env/env.h
IO/IOUtil.h
IO/SocketUtil.h
Macro/Array.h
mpih.cc
Options/CommonOptions.h
Options/CommonOptions.cc
)
add_executable (mpih ${SOURCE_FILES})
target_link_libraries(mpih "${MPI_C_LIBRARIES}" "${EVENT_LIBRARIES}")

if(GPERFTOOLS_FOUND)
	target_link_libraries(mpih "${GPERFTOOLS_LIBRARIES}")
endif()

#------------------------------------------------------------
# tests
#------------------------------------------------------------

enable_testing()
add_subdirectory(lib/gtest-1.7.0)
add_subdirectory(Test/UnitTest)
add_subdirectory(Test/IntegrationTest)

#------------------------------------------------------------
# install targets
#------------------------------------------------------------

install (TARGETS mpih DESTINATION bin)
