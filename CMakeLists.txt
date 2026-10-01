cmake_minimum_required(VERSION 3.31)
project(iiDecision VERSION 0.0.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_SHARED_LIBS "Build iiDecision as a shared library" OFF)
option(IIDECISION_BUILD_EXAMPLES "Build the first-candidate example" ON)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/SDK/iiDecision" CACHE PATH "Install prefix" FORCE)
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{USERPROFILE}/.local/SDK/iiDecision" CACHE PATH "Install prefix" FORCE)
    endif()
endif()

add_library(iiDecision
    src/FirstCandidateDecisionMaker.cpp
    src/ExactDecisionMaker.cpp
    src/Evaluation.cpp)
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
    target_compile_options(iiDecision PRIVATE /W4 /fp:strict)
else()
    target_compile_options(iiDecision PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
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
    foreach(_case SelectsFirstCandidate RejectsEmptyCandidates)
        add_test(NAME "FirstCandidateDecisionMaker.${_case}" COMMAND iiDecisionTests "${_case}")
        set_tests_properties("FirstCandidateDecisionMaker.${_case}" PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(iiDecisionProbabilityTests tests/probability.cpp)
    target_link_libraries(iiDecisionProbabilityTests PRIVATE iiDecision::iiDecision)
    foreach(_case Prior Conditioning Correlation RareEvidence LogRange Permutation Validation ZeroMass Tie Evaluation EvaluationValidation)
        add_test(NAME "iiDecision.${_case}" COMMAND iiDecisionProbabilityTests "${_case}")
        set_tests_properties("iiDecision.${_case}" PROPERTIES TIMEOUT 20)
    endforeach()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(iiDecisionOracleDriver tests/oracle_driver.cpp)
    target_link_libraries(iiDecisionOracleDriver PRIVATE iiDecision::iiDecision)
    add_test(NAME iiDecision.DecimalOracle COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/decimal_oracle.py"
        "$<TARGET_FILE:iiDecisionOracleDriver>" "${CMAKE_CURRENT_BINARY_DIR}/decimal-oracle.json")
    set_tests_properties(iiDecision.DecimalOracle PROPERTIES TIMEOUT 30)
endif()
if(IIDECISION_BUILD_EXAMPLES)
    add_executable(iiDecisionExample examples/first_candidate.cpp)
    target_link_libraries(iiDecisionExample PRIVATE iiDecision::iiDecision)
    add_executable(iiDecisionProbabilityExample examples/probability.cpp)
    target_link_libraries(iiDecisionProbabilityExample PRIVATE iiDecision::iiDecision)
    if(BUILD_TESTING)
        add_test(NAME iiDecision.Example COMMAND iiDecisionExample)
        set_tests_properties(iiDecision.Example PROPERTIES
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "^A")
        add_test(NAME iiDecision.ProbabilityExample COMMAND iiDecisionProbabilityExample)
        set_tests_properties(iiDecision.ProbabilityExample PROPERTIES
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "selected=A")
    endif()
endif()
