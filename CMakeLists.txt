cmake_minimum_required(VERSION 3.31)
project(iiDecision VERSION 0.0.5 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_SHARED_LIBS "Build iiDecision as a shared library" OFF)
option(IIDECISION_BUILD_EXAMPLES "Build the first-candidate example" ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
foreach(_backend MLX OPENCL)
    set(IIDECISION_HAS_${_backend} FALSE)
    set(IIDECISION_${_backend} "AUTO" CACHE STRING "${_backend} backend: AUTO, ON or OFF")
    set_property(CACHE IIDECISION_${_backend} PROPERTY STRINGS AUTO ON OFF)
    if(NOT IIDECISION_${_backend} MATCHES "^(AUTO|ON|OFF)$")
        message(FATAL_ERROR "IIDECISION_${_backend} must be AUTO, ON or OFF")
    endif()
endforeach()
find_package(Threads REQUIRED)

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
    src/Evaluation.cpp
    src/RagDecisionGate.cpp
    src/LayaEvidenceScorer.cpp
    src/LayaDecisionMaker.cpp
    src/Acceleration.cpp
    src/backends/Mlx.cpp
    src/backends/Metal.cpp
    src/backends/OpenCl.cpp
    src/backends/Vulkan.cpp)
add_library(iiDecision::iiDecision ALIAS iiDecision)
target_compile_features(iiDecision PUBLIC cxx_std_23)
target_link_libraries(iiDecision PRIVATE Threads::Threads)
if(NOT IIDECISION_MLX STREQUAL "OFF")
    find_package(MLX 0.32 CONFIG QUIET)
    if(MLX_FOUND)
        set(IIDECISION_HAS_MLX TRUE)
        target_compile_definitions(iiDecision PRIVATE IIDECISION_HAS_MLX)
        target_link_libraries(iiDecision PRIVATE mlx)
    elseif(IIDECISION_MLX STREQUAL "ON")
        message(FATAL_ERROR "MLX 0.32+ C++ SDK is required; set MLX_DIR")
    endif()
endif()
if(NOT IIDECISION_OPENCL STREQUAL "OFF")
    find_package(OpenCL QUIET)
    if(OpenCL_FOUND)
        set(IIDECISION_HAS_OPENCL TRUE)
        target_compile_definitions(iiDecision PRIVATE IIDECISION_HAS_OPENCL)
        target_link_libraries(iiDecision PRIVATE OpenCL::OpenCL)
    elseif(IIDECISION_OPENCL STREQUAL "ON")
        message(FATAL_ERROR "OpenCL SDK is required")
    endif()
endif()
foreach(_api METAL VULKAN)
    if(DEFINED IIDECISION_${_api} AND NOT IIDECISION_${_api} STREQUAL "OFF")
        message(WARNING "IIDECISION_${_api} no longer enables a built-in backend; supply the separately built application GPU adapter")
    endif()
endforeach()
if(BUILD_SHARED_LIBS)
    # Preserve dependency loader paths for installed consumers without exposing
    # vendor headers or compiler flags through the public C++ contract.
    if(IIDECISION_HAS_MLX)
        target_link_libraries(iiDecision INTERFACE "$<LINK_ONLY:mlx>")
    endif()
endif()
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
    target_compile_definitions(iiDecision PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
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
install(DIRECTORY examples/gpu-adapter DESTINATION "${CMAKE_INSTALL_DATADIR}/iiDecision/examples"
    PATTERN "*.iisacc.json" EXCLUDE PATTERN ".*.iisacc-preview" EXCLUDE PATTERN ".iisacc-temporary.lock" EXCLUDE)
install(FILES tests/acceleration.cpp tests/consumer/gpu.cpp
    DESTINATION "${CMAKE_INSTALL_DATADIR}/iiDecision/examples/gpu-adapter/tests")
install(DIRECTORY third_party/ DESTINATION "${CMAKE_INSTALL_DATADIR}/iiDecision/third_party")

if(BUILD_TESTING)
    add_test(NAME iiDecision.sdk_boundary COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_boundary.cmake")
    add_executable(iiDecisionRagTests tests/rag.cpp)
    target_link_libraries(iiDecisionRagTests PRIVATE iiDecision::iiDecision)
    foreach(_case Answerability Multimodal Ranking Boundaries Workflow Validation Ownership)
        add_test(NAME "iiDecision.Rag.${_case}" COMMAND iiDecisionRagTests "${_case}")
        set_tests_properties("iiDecision.Rag.${_case}" PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(iiDecisionLayaTests tests/laya.cpp)
    target_link_libraries(iiDecisionLayaTests PRIVATE iiDecision::iiDecision)
    add_test(NAME iiDecision.Laya.Retired COMMAND iiDecisionLayaTests)
    add_executable(iiDecisionAccelerationTests tests/acceleration.cpp)
    target_link_libraries(iiDecisionAccelerationTests PRIVATE iiDecision::iiDecision)
    foreach(_case Scalar Scheduling Integration)
        add_test(NAME "iiDecision.Acceleration.${_case}" COMMAND iiDecisionAccelerationTests "${_case}")
        set_tests_properties("iiDecision.Acceleration.${_case}" PROPERTIES TIMEOUT 60)
    endforeach()
    foreach(_backend mlx opencl)
        add_test(NAME "iiDecision.Acceleration.${_backend}" COMMAND iiDecisionAccelerationTests Devices "${_backend}/")
        set_tests_properties("iiDecision.Acceleration.${_backend}" PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
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
    set_tests_properties(iiDecision.DecimalOracle PROPERTIES TIMEOUT 120)
endif()
if(IIDECISION_BUILD_EXAMPLES)
    add_executable(iiDecisionRagExample examples/rag.cpp)
    target_link_libraries(iiDecisionRagExample PRIVATE iiDecision::iiDecision)
    add_executable(iiDecisionLayaExample examples/laya.cpp)
    target_link_libraries(iiDecisionLayaExample PRIVATE iiDecision::iiDecision)
    add_executable(iiDecisionExample examples/first_candidate.cpp)
    target_link_libraries(iiDecisionExample PRIVATE iiDecision::iiDecision)
    add_executable(iiDecisionProbabilityExample examples/probability.cpp)
    target_link_libraries(iiDecisionProbabilityExample PRIVATE iiDecision::iiDecision)
    add_executable(iiDecisionAccelerationExample examples/acceleration.cpp)
    target_link_libraries(iiDecisionAccelerationExample PRIVATE iiDecision::iiDecision)
    if(BUILD_TESTING)
        add_test(NAME iiDecision.RagExample COMMAND iiDecisionRagExample)
        set_tests_properties(iiDecision.RagExample PROPERTIES
            TIMEOUT 30 PASS_REGULAR_EXPRESSION "citation=guide:17")
        add_test(NAME iiDecision.Example COMMAND iiDecisionExample)
        set_tests_properties(iiDecision.Example PROPERTIES
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "^A")
        add_test(NAME iiDecision.ProbabilityExample COMMAND iiDecisionProbabilityExample)
        set_tests_properties(iiDecision.ProbabilityExample PROPERTIES
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "selected=A")
    endif()
endif()


configure_file(cmake/RemoveRetiredRuntimes.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/RemoveRetiredRuntimes.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/RemoveRetiredRuntimes.cmake")
