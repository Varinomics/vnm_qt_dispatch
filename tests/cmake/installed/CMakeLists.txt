cmake_minimum_required(VERSION 3.21)

project(vnm_qt_dispatch_installed_consumer LANGUAGES CXX)

include(CTest)

if(NOT DEFINED VNM_QT_DISPATCH_REQUESTED_VERSION)
    set(VNM_QT_DISPATCH_REQUESTED_VERSION "1.0")
endif()

find_package(
    vnm_qt_dispatch
    ${VNM_QT_DISPATCH_REQUESTED_VERSION}
    CONFIG
    REQUIRED)

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
