cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project(SiMBA++)

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

# SiMBAPass
add_library(SiMBAPass SHARED SiMBAPass.cpp)

set_target_properties(SiMBAPass PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)

set(PASS_LDFLAGS "-Wl,-undefined,dynamic_lookup -Wl,-no_pie")
target_link_libraries(SiMBAPass ${PASS_LDFLAGS})

# Install
install(TARGETS SiMBA++ SiMBAPass DESTINATION out)
