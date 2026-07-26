cmake_minimum_required(VERSION 3.21)

project(vnm_qt_dispatch_subproject_consumer LANGUAGES CXX)

include(CTest)

if(NOT DEFINED VNM_QT_DISPATCH_SOURCE_DIR)
    message(FATAL_ERROR "VNM_QT_DISPATCH_SOURCE_DIR is required.")
endif()

add_subdirectory(
    "${VNM_QT_DISPATCH_SOURCE_DIR}"
    vnm_qt_dispatch)

add_executable(
    vnm_qt_dispatch_subproject_consumer
    main.cpp)
target_link_libraries(
    vnm_qt_dispatch_subproject_consumer
    PRIVATE
        vnm_qt_dispatch::vnm_qt_dispatch)

add_test(
    NAME vnm_qt_dispatch_subproject_consumer_run
    COMMAND vnm_qt_dispatch_subproject_consumer)
