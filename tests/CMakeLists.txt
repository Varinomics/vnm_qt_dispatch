find_package(Qt6 ${VNM_QT_DISPATCH_QT_MIN_VERSION} REQUIRED COMPONENTS Test)

function(vnm_qt_dispatch_register_test test_name)
    if(WIN32)
        add_test(
            NAME ${test_name}
            COMMAND "${CMAKE_COMMAND}" -E env
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>$ENV{PATH}"
                ${ARGN})
    else()
        add_test(NAME ${test_name} COMMAND ${ARGN})
    endif()
endfunction()

function(vnm_qt_dispatch_add_qt_test target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(${target}
        PRIVATE
            vnm_qt_dispatch::vnm_qt_dispatch
            Qt6::Test
    )
    vnm_qt_dispatch_register_test(
        ${target}
        "$<TARGET_FILE:${target}>")
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
vnm_qt_dispatch_register_test(
    vnm_qt_dispatch_tls_exit_tests
    "$<TARGET_FILE:vnm_qt_dispatch_tls_exit_tests>")
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
    "-DTEST_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
    "-DTEST_INSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}"
)
if(DEFINED QT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT)
    string(CONCAT qt_minimum_argument
        "-DQT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT="
        "${QT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT}")
    list(APPEND vnm_qt_dispatch_nested_test_arguments
        "${qt_minimum_argument}")
endif()

vnm_qt_dispatch_register_test(
    vnm_qt_dispatch_subproject_consumer
    "${CMAKE_COMMAND}"
        ${vnm_qt_dispatch_nested_test_arguments}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_subproject_consumer.cmake")
set_tests_properties(
    vnm_qt_dispatch_subproject_consumer
    PROPERTIES
        TIMEOUT 300)

vnm_qt_dispatch_register_test(
    vnm_qt_dispatch_installed_consumer
    "${CMAKE_COMMAND}"
        ${vnm_qt_dispatch_nested_test_arguments}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_consumer.cmake")
set_tests_properties(
    vnm_qt_dispatch_installed_consumer
    PROPERTIES
        TIMEOUT 300
        RUN_SERIAL TRUE)
