cmake_minimum_required(VERSION 3.21)

project(vnm_qt_dispatch_installed_consumer LANGUAGES CXX)

include(CTest)

if(NOT DEFINED VNM_QT_DISPATCH_REQUESTED_VERSION)
    set(VNM_QT_DISPATCH_REQUESTED_VERSION "2.0")
endif()
if(NOT DEFINED VNM_QT_DISPATCH_EXPECTED_DIR)
    message(FATAL_ERROR "VNM_QT_DISPATCH_EXPECTED_DIR is required.")
endif()

if(VNM_QT_DISPATCH_EXPECT_INCOMPATIBLE)
    find_package(
        vnm_qt_dispatch
        ${VNM_QT_DISPATCH_REQUESTED_VERSION}
        CONFIG
        QUIET
        PATHS "${VNM_QT_DISPATCH_EXPECTED_DIR}"
        NO_DEFAULT_PATH)
    if(vnm_qt_dispatch_FOUND)
        message(FATAL_ERROR
            "The staged vnm_qt_dispatch package accepted an incompatible "
            "version.")
    endif()

    set(expected_provider_config
        "${VNM_QT_DISPATCH_EXPECTED_DIR}/vnm_qt_dispatchConfig.cmake")
    file(REAL_PATH "${expected_provider_config}" expected_provider_config)
    set(resolved_considered_configs)
    foreach(considered_config IN LISTS vnm_qt_dispatch_CONSIDERED_CONFIGS)
        file(REAL_PATH "${considered_config}" considered_config)
        list(APPEND resolved_considered_configs "${considered_config}")
    endforeach()
    list(REMOVE_DUPLICATES resolved_considered_configs)
    if(NOT resolved_considered_configs STREQUAL expected_provider_config)
        message(FATAL_ERROR
            "The incompatible probe considered a provider outside the staged "
            "prefix:\n"
            "  expected=${expected_provider_config}\n"
            "  actual=${resolved_considered_configs}")
    endif()
    message(FATAL_ERROR
        "The staged vnm_qt_dispatch package is incompatible with the "
        "requested version.")
else()
    find_package(
        vnm_qt_dispatch
        ${VNM_QT_DISPATCH_REQUESTED_VERSION}
        CONFIG
        REQUIRED
        PATHS "${VNM_QT_DISPATCH_EXPECTED_DIR}"
        NO_DEFAULT_PATH)
endif()

file(REAL_PATH "${vnm_qt_dispatch_DIR}" resolved_provider_dir)
file(REAL_PATH "${VNM_QT_DISPATCH_EXPECTED_DIR}" expected_provider_dir)
if(NOT resolved_provider_dir STREQUAL expected_provider_dir)
    message(FATAL_ERROR
        "vnm_qt_dispatch resolved outside the staged provider prefix:\n"
        "  expected=${expected_provider_dir}\n"
        "  actual=${resolved_provider_dir}")
endif()

get_target_property(
    vnm_qt_dispatch_interface_links
    vnm_qt_dispatch::vnm_qt_dispatch
    INTERFACE_LINK_LIBRARIES)
if(NOT "${vnm_qt_dispatch_interface_links}" STREQUAL "Qt6::Core")
    message(FATAL_ERROR
        "vnm_qt_dispatch must expose only Qt6::Core, got: "
        "${vnm_qt_dispatch_interface_links}")
endif()

get_target_property(
    vnm_qt_dispatch_interface_features
    vnm_qt_dispatch::vnm_qt_dispatch
    INTERFACE_COMPILE_FEATURES)
if(NOT "cxx_std_20" IN_LIST vnm_qt_dispatch_interface_features)
    message(FATAL_ERROR
        "vnm_qt_dispatch must expose the cxx_std_20 compile feature.")
endif()

add_executable(
    vnm_qt_dispatch_installed_consumer
    main.cpp)
target_compile_definitions(
    vnm_qt_dispatch_installed_consumer
    PRIVATE
        QT_NO_KEYWORDS)
target_link_libraries(
    vnm_qt_dispatch_installed_consumer
    PRIVATE
        vnm_qt_dispatch::vnm_qt_dispatch)

add_test(
    NAME vnm_qt_dispatch_installed_consumer_run
    COMMAND vnm_qt_dispatch_installed_consumer)
