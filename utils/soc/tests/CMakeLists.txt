set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt5Test REQUIRED)

add_custom_target(copy-test-files ALL
  COMMAND ${CMAKE_COMMAND} -E rm -r ${CMAKE_CURRENT_BINARY_DIR}/test_cases
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/test_cases/ ${CMAKE_CURRENT_BINARY_DIR}/test_cases
)

add_executable(soc_namespace tst_namespace.cpp)
add_test(NAME soc_namespace COMMAND mytest)
target_link_libraries(soc_namespace PRIVATE Qt5::Test soc_lib)
add_dependencies(soc_namespace copy-test-files)

