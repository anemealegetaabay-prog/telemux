file(GLOB TELEMUX_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

add_executable(telemux_tests test_main.cpp ${TELEMUX_TEST_SOURCES})
target_link_libraries(telemux_tests PRIVATE telemux)
target_include_directories(telemux_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME telemux_tests COMMAND telemux_tests)
