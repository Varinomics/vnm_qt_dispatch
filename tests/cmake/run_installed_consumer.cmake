cmake_minimum_required(VERSION 3.21)

foreach(required_variable
    PROVIDER_BINARY_DIR
    TEST_BINARY_ROOT
    TEST_CONFIG
    TEST_GENERATOR
    TEST_CXX_COMPILER
    TEST_QT_DIR
    TEST_INSTALL_LIBDIR
    TEST_INSTALL_DATADIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

set(test_prefix "${TEST_BINARY_ROOT}/prefix-${TEST_CONFIG}")
set(test_binary_dir "${TEST_BINARY_ROOT}/installed-${TEST_CONFIG}")
set(incompatible_binary_dir
    "${TEST_BINARY_ROOT}/installed-incompatible-${TEST_CONFIG}")
set(provider_package_dir
    "${test_prefix}/${TEST_INSTALL_LIBDIR}/cmake/vnm_qt_dispatch")
set(provider_license
    "${test_prefix}/${TEST_INSTALL_DATADIR}/licenses/vnm_qt_dispatch/LICENSE")

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
if(NOT EXISTS "${provider_license}")
    message(FATAL_ERROR
        "The provider license was not installed at the conventional path:\n"
        "  ${provider_license}")
endif()

function(configure_consumer
    binary_dir
    requested_version
    expect_incompatible
    result_variable
    log_variable)
    set(configure_command
        "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_LIST_DIR}/installed"
        -B "${binary_dir}"
        -G "${TEST_GENERATOR}"
        "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
        "-DQt6_DIR=${TEST_QT_DIR}"
        "-Dvnm_qt_dispatch_DIR:PATH=${provider_package_dir}"
        "-DVNM_QT_DISPATCH_EXPECTED_DIR:PATH=${provider_package_dir}"
        "-DCMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE"
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE"
        "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
        "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=TRUE"
        "-DCMAKE_BUILD_TYPE=${TEST_CONFIG}"
        "-DVNM_QT_DISPATCH_REQUESTED_VERSION=${requested_version}")
    if(expect_incompatible)
        list(APPEND configure_command
            "-DVNM_QT_DISPATCH_EXPECT_INCOMPATIBLE=TRUE")
    endif()
    if(NOT "${TEST_GENERATOR_PLATFORM}" STREQUAL "")
        list(APPEND configure_command -A "${TEST_GENERATOR_PLATFORM}")
    endif()
    if(NOT "${TEST_CMAKE_MAKE_PROGRAM}" STREQUAL "")
        list(APPEND configure_command
            "-DCMAKE_MAKE_PROGRAM=${TEST_CMAKE_MAKE_PROGRAM}")
    endif()
    if(DEFINED QT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT)
        string(CONCAT qt_minimum_argument
            "-DQT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT="
            "${QT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT}")
        list(APPEND configure_command
            "${qt_minimum_argument}")
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

function(read_consumer_cache binary_dir output_variable)
    set(cache_file "${binary_dir}/CMakeCache.txt")
    if(NOT EXISTS "${cache_file}")
        message(FATAL_ERROR
            "The consumer configure did not create a cache:\n"
            "  ${cache_file}")
    endif()
    file(READ "${cache_file}" cache_contents)
    set(${output_variable} "${cache_contents}" PARENT_SCOPE)
endfunction()

function(assert_expected_provider_cache binary_dir)
    read_consumer_cache("${binary_dir}" cache_contents)
    file(TO_CMAKE_PATH "${provider_package_dir}" expected_cache_dir)
    if(NOT cache_contents MATCHES
       "VNM_QT_DISPATCH_EXPECTED_DIR:PATH=${expected_cache_dir}([\r\n]|$)")
        message(FATAL_ERROR
            "The consumer was not constrained to the staged provider "
            "package:\n"
            "  expected=${expected_cache_dir}")
    endif()
endfunction()

function(assert_provider_cache binary_dir)
    assert_expected_provider_cache("${binary_dir}")
    read_consumer_cache("${binary_dir}" cache_contents)
    set(cache_file "${binary_dir}/CMakeCache.txt")
    file(TO_CMAKE_PATH "${provider_package_dir}" expected_cache_dir)
    if(NOT cache_contents MATCHES
       "vnm_qt_dispatch_DIR:PATH=${expected_cache_dir}([\r\n]|$)")
        message(FATAL_ERROR
            "The consumer did not resolve vnm_qt_dispatch from the staged "
            "provider package:\n"
            "  expected=${expected_cache_dir}\n"
            "  cache=${cache_file}")
    endif()
endfunction()

configure_consumer(
    "${test_binary_dir}"
    "2"
    FALSE
    configure_result
    configure_log)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "The installed consumer configure failed: ${configure_result}\n"
        "${configure_log}")
endif()
assert_provider_cache("${test_binary_dir}")

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
    "3.0"
    TRUE
    incompatible_configure_result
    incompatible_configure_log)
assert_expected_provider_cache("${incompatible_binary_dir}")
if(incompatible_configure_result EQUAL 0)
    message(FATAL_ERROR
        "The installed package accepted an incompatible major version.")
endif()
if(NOT incompatible_configure_log MATCHES
   "staged vnm_qt_dispatch package is incompatible")
    message(FATAL_ERROR
        "The incompatible package configure failed for an unexpected reason:\n"
        "${incompatible_configure_log}")
endif()
