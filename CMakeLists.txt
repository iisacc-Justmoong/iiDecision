cmake_minimum_required(VERSION 3.31)
project(iiDecision VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_SHARED_LIBS "Build iiDecision as a shared library" OFF)
option(IIDECISION_BUILD_EXAMPLES "Build the decision routing example" ON)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/SDK/iiDecision" CACHE PATH "Install prefix" FORCE)
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{USERPROFILE}/.local/SDK/iiDecision" CACHE PATH "Install prefix" FORCE)
    endif()
endif()

add_library(iiDecision src/DecisionModel.cpp)
add_library(iiDecision::iiDecision ALIAS iiDecision)
target_compile_features(iiDecision PUBLIC cxx_std_23)
target_include_directories(iiDecision PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(iiDecision PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
    target_compile_options(iiDecision PRIVATE /W4)
else()
    target_compile_options(iiDecision PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS iiDecision EXPORT iiDecisionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/iiDecision DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiDecision")
configure_package_config_file(cmake/iiDecisionConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiDecisionConfig.cmake"
    INSTALL_DESTINATION "${_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiDecisionConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT iiDecisionTargets NAMESPACE iiDecision:: DESTINATION "${_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiDecisionConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiDecisionConfigVersion.cmake" DESTINATION "${_package_dir}")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiDecision")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/iiDecision/docs")

if(BUILD_TESTING)
    add_executable(iiDecisionTests tests/decision.cpp)
    target_link_libraries(iiDecisionTests PRIVATE iiDecision::iiDecision)
    foreach(_case probabilities validation training atomic determinism)
        add_test(NAME "iiDecision.${_case}" COMMAND iiDecisionTests "${_case}")
        set_tests_properties("iiDecision.${_case}" PROPERTIES TIMEOUT 20)
    endforeach()
endif()
if(IIDECISION_BUILD_EXAMPLES)
    add_executable(iiDecisionRouting examples/routing.cpp)
    target_link_libraries(iiDecisionRouting PRIVATE iiDecision::iiDecision)
    if(BUILD_TESTING)
        add_test(NAME iiDecision.Routing COMMAND iiDecisionRouting)
        set_tests_properties(iiDecision.Routing PROPERTIES
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "selected=local")
    endif()
endif()
