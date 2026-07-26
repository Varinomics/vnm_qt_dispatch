cmake_minimum_required(VERSION 3.21)

project(vnm_qt_dispatch LANGUAGES CXX VERSION 1.0.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(vnm_qt_dispatch_build_testing_default OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(vnm_qt_dispatch_build_testing_default ON)
endif()

option(VNM_QT_DISPATCH_BUILD_TESTING
    "Build vnm_qt_dispatch tests"
    ${vnm_qt_dispatch_build_testing_default})

set(VNM_QT_DISPATCH_QT_MIN_VERSION "6.5" CACHE STRING
    "Minimum Qt 6 version required by vnm_qt_dispatch")

find_package(Qt6 ${VNM_QT_DISPATCH_QT_MIN_VERSION} REQUIRED COMPONENTS Core)

add_library(vnm_qt_dispatch INTERFACE)
add_library(vnm_qt_dispatch::vnm_qt_dispatch ALIAS vnm_qt_dispatch)

target_compile_features(vnm_qt_dispatch INTERFACE cxx_std_20)
target_include_directories(vnm_qt_dispatch
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(vnm_qt_dispatch INTERFACE Qt6::Core)

install(TARGETS vnm_qt_dispatch
    EXPORT vnm_qt_dispatchTargets
)
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(FILES LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/vnm_qt_dispatch"
)
install(EXPORT vnm_qt_dispatchTargets
    FILE vnm_qt_dispatchTargets.cmake
    NAMESPACE vnm_qt_dispatch::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vnm_qt_dispatch"
)

configure_package_config_file(
    cmake/vnm_qt_dispatchConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vnm_qt_dispatchConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vnm_qt_dispatch"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vnm_qt_dispatchConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vnm_qt_dispatchConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vnm_qt_dispatchConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vnm_qt_dispatch"
)

if(VNM_QT_DISPATCH_BUILD_TESTING)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
