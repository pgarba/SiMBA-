cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project(SiMBA++)

INCLUDE(CTest)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS ${LLVM_TOOLS_BINARY_DIR})

include_directories(${LLVM_INCLUDE_DIRS} "include")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(SiMBA++ "CSiMBA.cpp" "LLVMParser.cpp" "LLVMParser.h" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp")

target_link_libraries(SiMBA++
    LLVMipo
    LLVMScalarOpts
    LLVMInstCombine
    LLVMTransformUtils
    LLVMAnalysis
    LLVMTarget
    LLVMOption
    LLVMObject
    LLVMBitReader
    LLVMCore
    LLVMSupport
)

# SiMBA++ tests
ENABLE_TESTING()
include(FindPythonInterp)

add_test(NAME Convert_DATA_to_LLVM
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/convert_data_to_llvm.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts)

add_test(NAME MBAs_Linear_64bit_unsigned_llvm_detect_and_simplify
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests_llvm_simplify.py "64" "0"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME MBAs_Linear_64bit_unsigned
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py "64" "0"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME MBAs_Linear_64bit_unsigned_llvm
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests_llvm.py "64" "0"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# SiMBAPass
add_library(SiMBAPass SHARED SiMBAPass.cpp SiMBAPass.h LLVMParser.cpp LLVMParser.h Simplifier.h Simplifier.cpp ShuttingYard.cpp MBAChecker.cpp MBAChecker.h)

set_target_properties(SiMBAPass PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)

if(APPLE)
    set(PASS_LDFLAGS "-Wl,-undefined,dynamic_lookup -Wl,-no_pie")
elseif(WIN32)
    target_link_libraries(SiMBAPass
        LLVMipo
        LLVMScalarOpts
        LLVMInstCombine
        LLVMTransformUtils
        LLVMAnalysis
        LLVMTarget
        LLVMOption
        LLVMObject
        LLVMBitReader
        LLVMCore
        LLVMSupport
    )
endif()

target_link_libraries(SiMBAPass ${PASS_LDFLAGS})

# Install
install(TARGETS SiMBA++ SiMBAPass DESTINATION out)
