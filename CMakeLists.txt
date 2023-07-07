cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project(SiMBA++)

set(CMAKE_CXX_STANDARD 17)

# set(LLVM_DIR "c:/libs/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM binary directory: " ${LLVM_TOOLS_BINARY_DIR})

message(STATUS ${LLVM_TOOLS_BINARY_DIR})

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --system-libs OUTPUT_VARIABLE SYS_LIBS)
execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --ldflags OUTPUT_VARIABLE LDF)

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --cxxflags OUTPUT_VARIABLE CMAKE_CXX_FLAGS)
string(STRIP ${CMAKE_CXX_FLAGS} CMAKE_CXX_FLAGS)

string(STRIP ${LLVM_LIBS} LLVM_LIBS)
string(STRIP ${SYS_LIBS} SYS_LIBS)
string(STRIP ${LDF} LDF)

set(LLVM_LIBS ${LLVM_LIBS} ${SYS_LIBS})
set(LLVM_LIBS ${LLVM_LIBS} ${LDF})

# z3
find_package(Z3 REQUIRED CONFIG)

message(STATUS "Z3 include directory: " ${Z3_CXX_INCLUDE_DIRS})
message(STATUS "Z3 library: " ${Z3_LIBRARIES})
include_directories(${Z3_CXX_INCLUDE_DIRS} "include")

# Disable some LLVM warnings for Visual Studio
if(WIN32)
    add_definitions("/wd4624" "/wd4146" "/wd4244" "/wd4267")
endif()

include_directories(${LLVM_INCLUDE_DIRS} "include")

#separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#add_definitions(${LLVM_DEFINITIONS_LIST})

# add llvm at end so all linkers can find it
list(APPEND PROJECT_LIBRARIES ${LLVM_LIBS})
list(APPEND PROJECT_DEFINITIONS ${LLVM_DEFINITIONS})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${LLVM_INCLUDE_DIRS})


add_executable(SiMBA++ "CSiMBA.cpp" "CSiMBA.h" "LLVMParser.cpp" "LLVMParser.h" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp" "Modulo.cpp" "Modulo.h" "Z3Prover.cpp" "Z3Prover.h")

#set_target_properties(SiMBA++ PROPERTIES
#    COMPILE_FLAGS " -fno-rtti"
#)

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
    ${Z3_LIBRARIES}
)

# SiMBA++ static
add_library(LSiMBA++ STATIC "LLVMParser.cpp" "LLVMParser.h" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp" "Modulo.cpp" "Modulo.h" "Z3Prover.cpp" "Z3Prover.h")

# No rtti
#set_target_properties(LSiMBA++ PROPERTIES
#    COMPILE_FLAGS " -fno-rtti"
#)

# SiMBA++ tests
INCLUDE(CTest)
ENABLE_TESTING()
include(FindPythonInterp)

add_test(NAME Convert_DATA_to_LLVM
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/convert_data_to_llvm.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts)

add_test(NAME MBAs_Linear_64bit_unsigned_llvm_detect_and_simplify
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests_llvm_simplify.py " 64" "0 "
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME MBAs_Linear_64bit_unsigned
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py " 64" "0 "
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME MBAs_Linear_64bit_unsigned_llvm
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests_llvm.py " 64" "0 "
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# SiMBAPass
add_library(SiMBAPass SHARED SiMBAPass.cpp SiMBAPass.h LLVMParser.cpp LLVMParser.h Simplifier.h Simplifier.cpp ShuttingYard.cpp MBAChecker.cpp MBAChecker.h Modulo.cpp Modulo.h)

# No rtti
set_target_properties(SiMBAPass PROPERTIES
    COMPILE_FLAGS " -fno-rtti"
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

# Header
set_target_properties(LSiMBA++ PROPERTIES PUBLIC_HEADER "LLVMParser.h;include/splitmix64.h")

# Install
install(TARGETS SiMBA++ SiMBAPass DESTINATION "bin"
	LIBRARY DESTINATION "lib"
	PUBLIC_HEADER DESTINATION "include"
	)

install(TARGETS LSiMBA++
        LIBRARY DESTINATION "lib"
        PUBLIC_HEADER DESTINATION "include"
        )
