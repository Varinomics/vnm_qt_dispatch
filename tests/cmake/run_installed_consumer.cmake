cmake_minimum_required(VERSION 3.21)

foreach(required_variable
    PROVIDER_BINARY_DIR
    TEST_BINARY_ROOT
    TEST_CONFIG
    TEST_GENERATOR
    TEST_CXX_COMPILER
    TEST_QT_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

set(test_prefix "${TEST_BINARY_ROOT}/prefix-${TEST_CONFIG}")
set(test_binary_dir "${TEST_BINARY_ROOT}/installed-${TEST_CONFIG}")
set(incompatible_binary_dir
    "${TEST_BINARY_ROOT}/installed-incompatible-${TEST_CONFIG}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        --install "${PROVIDER_BINARY_DIR}"
        --prefix "${test_prefix}"
        --config "${TEST_CONFIG}"
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "The provider install failed: ${install_result}")
endif()

function(configure_consumer
    binary_dir
    requested_version
    result_variable
    log_variable)
    set(configure_command
        "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_LIST_DIR}/installed"
        -B "${binary_dir}"
        -G "${TEST_GENERATOR}"
        "-DCMAKE_PREFIX_PATH=${test_prefix}"
        "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
        "-DQt6_DIR=${TEST_QT_DIR}"
        "-DCMAKE_BUILD_TYPE=${TEST_CONFIG}"
        "-DVNM_QT_DISPATCH_REQUESTED_VERSION=${requested_version}")
    if(NOT "${TEST_GENERATOR_PLATFORM}" STREQUAL "")
        list(APPEND configure_command -A "${TEST_GENERATOR_PLATFORM}")
    endif()
    if(NOT "${TEST_CMAKE_MAKE_PROGRAM}" STREQUAL "")
        list(APPEND configure_command
            "-DCMAKE_MAKE_PROGRAM=${TEST_CMAKE_MAKE_PROGRAM}")
    endif()

    execute_process(
        COMMAND ${configure_command}
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_output
        ERROR_VARIABLE configure_error)
    set(${result_variable} "${configure_result}" PARENT_SCOPE)
    set(
        ${log_variable}
        "${configure_output}\n${configure_error}"
        PARENT_SCOPE)
endfunction()

configure_consumer(
    "${test_binary_dir}"
    "1"
    configure_result
    configure_log)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "The installed consumer configure failed: ${configure_result}\n"
        "${configure_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${test_binary_dir}"
        --config "${TEST_CONFIG}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "The installed consumer build failed: ${build_result}")
endif()

execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}"
        --test-dir "${test_binary_dir}"
        -C "${TEST_CONFIG}"
        --output-on-failure
    RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR
        "The installed consumer test failed: ${test_result}")
endif()

configure_consumer(
    "${incompatible_binary_dir}"
    "2.0"
    incompatible_configure_result
    incompatible_configure_log)
if(incompatible_configure_result EQUAL 0)
    message(FATAL_ERROR
        "The installed package accepted an incompatible major version.")
endif()
if(NOT incompatible_configure_log MATCHES
   "compatible with requested version")
    message(FATAL_ERROR
        "The incompatible package configure failed for an unexpected reason:\n"
        "${incompatible_configure_log}")
endif()
