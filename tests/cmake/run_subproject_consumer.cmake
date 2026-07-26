cmake_minimum_required(VERSION 3.21)

foreach(required_variable
    PROVIDER_SOURCE_DIR
    TEST_BINARY_ROOT
    TEST_CONFIG
    TEST_GENERATOR
    TEST_CXX_COMPILER
    TEST_QT_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

set(test_binary_dir "${TEST_BINARY_ROOT}/subproject-${TEST_CONFIG}")

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/subproject"
    -B "${test_binary_dir}"
    -G "${TEST_GENERATOR}"
    "-DVNM_QT_DISPATCH_SOURCE_DIR=${PROVIDER_SOURCE_DIR}"
    "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
    "-DQt6_DIR=${TEST_QT_DIR}"
    "-DCMAKE_BUILD_TYPE=${TEST_CONFIG}")
if(NOT "${TEST_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND configure_command -A "${TEST_GENERATOR_PLATFORM}")
endif()
if(NOT "${TEST_CMAKE_MAKE_PROGRAM}" STREQUAL "")
    list(APPEND configure_command
        "-DCMAKE_MAKE_PROGRAM=${TEST_CMAKE_MAKE_PROGRAM}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "The source-subproject consumer configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${test_binary_dir}"
        --config "${TEST_CONFIG}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "The source-subproject consumer build failed: ${build_result}")
endif()

execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}"
        --test-dir "${test_binary_dir}"
        -C "${TEST_CONFIG}"
        --output-on-failure
    RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR
        "The source-subproject consumer test failed: ${test_result}")
endif()
