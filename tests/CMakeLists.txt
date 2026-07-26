find_package(Qt6 ${VNM_QT_DISPATCH_QT_MIN_VERSION} REQUIRED COMPONENTS Test)

function(vnm_qt_dispatch_add_qt_test target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(${target}
        PRIVATE
            vnm_qt_dispatch::vnm_qt_dispatch
            Qt6::Test
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60)
endfunction()

vnm_qt_dispatch_add_qt_test(
    vnm_qt_dispatch_tests
    vnm_qt_dispatch_tests.cpp)
vnm_qt_dispatch_add_qt_test(
    vnm_qt_dispatch_default_reporter_tests
    vnm_qt_dispatch_default_reporter_tests.cpp)

add_executable(
    vnm_qt_dispatch_tls_exit_tests
    vnm_qt_dispatch_tls_exit_tests.cpp)
target_compile_definitions(
    vnm_qt_dispatch_tls_exit_tests
    PRIVATE
        QT_NO_KEYWORDS)
target_link_libraries(
    vnm_qt_dispatch_tls_exit_tests
    PRIVATE
        vnm_qt_dispatch::vnm_qt_dispatch)
add_test(
    NAME vnm_qt_dispatch_tls_exit_tests
    COMMAND vnm_qt_dispatch_tls_exit_tests)
set_tests_properties(
    vnm_qt_dispatch_tls_exit_tests
    PROPERTIES
        TIMEOUT 120)

set(vnm_qt_dispatch_nested_test_arguments
    "-DPROVIDER_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DPROVIDER_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DTEST_BINARY_ROOT=${PROJECT_BINARY_DIR}/nested-tests"
    "-DTEST_CONFIG=$<CONFIG>"
    "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
    "-DTEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DTEST_CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DTEST_QT_DIR=${Qt6_DIR}"
)

add_test(
    NAME vnm_qt_dispatch_subproject_consumer
    COMMAND "${CMAKE_COMMAND}"
        ${vnm_qt_dispatch_nested_test_arguments}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_subproject_consumer.cmake")
set_tests_properties(
    vnm_qt_dispatch_subproject_consumer
    PROPERTIES
        TIMEOUT 300)

add_test(
    NAME vnm_qt_dispatch_installed_consumer
    COMMAND "${CMAKE_COMMAND}"
        ${vnm_qt_dispatch_nested_test_arguments}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_consumer.cmake")
set_tests_properties(
    vnm_qt_dispatch_installed_consumer
    PROPERTIES
        TIMEOUT 300
        RUN_SERIAL TRUE)
