#------------------------------------------------------------
# google test setup
#------------------------------------------------------------

include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

#------------------------------------------------------------
# unit tests
#------------------------------------------------------------

add_executable(MPIChannelTest MPIChannelTest.cc)
target_link_libraries(MPIChannelTest gtest gtest_main)
add_test(MPIChannelTest MPIChannelTest)
