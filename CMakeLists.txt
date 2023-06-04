cmake_minimum_required(VERSION 3.14)
project(polymorphic_value)
set(CMAKE_CXX_STANDARD 20)

add_executable(test_polymorphic_value polymorphic_value.h test_polymorphic_value.cpp)

set_target_properties(test_polymorphic_value
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

enable_testing()
add_test(
    NAME polymorphic_test
    COMMAND test_polymorphic_value
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)