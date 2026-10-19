# C/C++ SDK archives and vendor packages expose different config layouts.
find_path(ONNXRuntime_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${ONNXRuntime_ROOT}" ENV ONNXRuntime_ROOT
    PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session)
find_library(ONNXRuntime_LIBRARY NAMES onnxruntime
    HINTS "${ONNXRuntime_ROOT}" ENV ONNXRuntime_ROOT PATH_SUFFIXES lib lib64)
if(ONNXRuntime_INCLUDE_DIR)
    file(STRINGS "${ONNXRuntime_INCLUDE_DIR}/onnxruntime_c_api.h" _api_line REGEX "^#define ORT_API_VERSION [0-9]+")
    string(REGEX REPLACE ".*ORT_API_VERSION ([0-9]+).*" "\\1" ONNXRuntime_API_VERSION "${_api_line}")
endif()
set(_onnx_version_ok FALSE)
if(ONNXRuntime_API_VERSION GREATER_EQUAL 24)
    set(_onnx_version_ok TRUE)
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime REQUIRED_VARS ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY _onnx_version_ok)
if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
    if(WIN32)
        find_file(ONNXRuntime_RUNTIME NAMES onnxruntime.dll
            HINTS "${ONNXRuntime_ROOT}" ENV ONNXRuntime_ROOT PATH_SUFFIXES lib bin NO_DEFAULT_PATH)
        if(NOT ONNXRuntime_RUNTIME)
            message(FATAL_ERROR "ONNX Runtime's matching Windows DLL is required")
        endif()
        add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRuntime_LIBRARY}" IMPORTED_LOCATION "${ONNXRuntime_RUNTIME}")
    else()
        add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}")
    endif()
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}")
endif()
if(WIN32 AND ONNXRuntime_FOUND)
    function(onnxruntime_deploy target)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>"
                "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endfunction()
endif()
mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY)
