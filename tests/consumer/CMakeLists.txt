cmake_minimum_required(VERSION 3.31)
project(iiDecisionInstalledConsumer LANGUAGES CXX)
include(CTest)
find_package(iiDecision 0.0.4 EXACT CONFIG REQUIRED)
add_executable(iiDecisionConsumer main.cpp)
target_link_libraries(iiDecisionConsumer PRIVATE iiDecision::iiDecision)
add_test(NAME iiDecision.InstalledConsumer COMMAND iiDecisionConsumer)
set(IIDECISION_CONSUMER_LAYA_PACKAGE "" CACHE PATH "Laya contract fixture for the installed consumer")
if(IIDECISION_CONSUMER_LAYA_PACKAGE)
    add_test(NAME iiDecision.InstalledLayaConsumer COMMAND "${CMAKE_COMMAND}" -E env
        --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH
        "$<TARGET_FILE:iiDecisionConsumer>" "${IIDECISION_CONSUMER_LAYA_PACKAGE}")
    set_tests_properties(iiDecision.InstalledLayaConsumer PROPERTIES TIMEOUT 60)
endif()
