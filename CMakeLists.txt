cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project(SiMBA++)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS ${LLVM_TOOLS_BINARY_DIR})

include_directories(${LLVM_INCLUDE_DIRS} "include")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(SiMBA++ "CSiMBA.cpp" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp")

target_link_libraries(SiMBA++ 
LLVMipo
LLVMScalarOpts
LLVMInstCombine
LLVMTransformUtils
LLVMAnalysis
LLVMTarget
LLVMOption
LLVMMC
LLVMObject 
LLVMBitReader 
LLVMCore
LLVMSupport
)

install(TARGETS SiMBA++ DESTINATION out)
