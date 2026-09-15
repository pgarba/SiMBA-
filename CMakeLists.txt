cmake_minimum_required(VERSION 3.5.0 FATAL_ERROR)

project(SiMBA++)
set(CMAKE_CXX_STANDARD 17)

# MBA_HAS_Z3 is set per-target (see target_compile_definitions below) rather
# than globally: SiMBA++/LSiMBA++/SiMBAPass link Z3 and get the full proof
# path in MBA/Verify.cpp, while mba_cli does not link Z3 and gets a safe no-op
# proveEquivalent.
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # Default location of a prebuilt LLVM+Z3 tree on Windows. Override with
    # -DBuildDir=... (or set LLVM_DIR/Z3_INCLUDE_DIRS/Z3_LIBRARIES directly)
    # if LLVM/Z3 are installed somewhere else, instead of editing this file.
    set(BuildDir "c:/libs" CACHE PATH "Root directory of a prebuilt Windows LLVM/Z3 tree")
    if(NOT DEFINED LLVM_DIR)
        set(LLVM_DIR "${BuildDir}/lib/cmake/llvm/" CACHE PATH "Directory containing LLVMConfig.cmake")
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${BuildDir}/")
    if(NOT DEFINED Z3_INCLUDE_DIRS)
        set(Z3_INCLUDE_DIRS "${BuildDir}/include/")
    endif()
    if(NOT DEFINED Z3_LIBRARIES)
        set(Z3_LIBRARIES "${BuildDir}/lib/libz3.lib")
    endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if(NOT DEFINED Z3_INCLUDE_DIRS)
        set(Z3_INCLUDE_DIRS "/usr/include")
    endif()
endif()

#https://github.com/JonathanSalwan/Triton/blob/master/CMakeLists.txt#L177
#Compiling with LLVM
message(STATUS "Compiling with LLVM")
if(NOT DEFINED LLVM_INCLUDE_DIRS)
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    include_directories(${LLVM_INCLUDE_DIRS})
    if(LLVM_LINK_LLVM_DYLIB)
        set(LLVM_LIBRARIES LLVM)
    else()
        set(LLVM_LIBRARIES ${LLVM_AVAILABLE_LIBS})
    endif()
endif()


#find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM binary directory: " ${LLVM_TOOLS_BINARY_DIR})

message(STATUS ${LLVM_TOOLS_BINARY_DIR})

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --system-libs OUTPUT_VARIABLE SYS_LIBS)
execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --ldflags OUTPUT_VARIABLE LDF)

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --cxxflags OUTPUT_VARIABLE CMAKE_CXX_FLAGS)
if(CMAKE_CXX_FLAGS)
    string(STRIP ${CMAKE_CXX_FLAGS} CMAKE_CXX_FLAGS)
endif()

# llvm-config's own --cxxflags disables C++ exceptions (LLVM's own build
# convention, e.g. /EHs-c- on MSVC/clang-cl) - fine for LLVM's own code,
# but z3++.h's entire error-handling model is built on throwing on API
# errors (see check_error()/Z3_THROW): with exceptions compiled out,
# Z3_THROW silently becomes a no-op instead. Re-enabling exceptions here
# restores that intended path, letting a try/catch around Z3-proving call
# sites turn an ordinary Z3 API-level error (Z3_get_error_code != Z3_OK)
# into a graceful "not proved" instead of silently continuing with
# whatever the C API returned on failure.
#
# Confirmed NOT sufficient on its own for the specific crash this was
# investigated alongside (a segfault inside Z3_inc_ref on an otherwise
# completely ordinary bv_const call, on SiMBA-heavy targets like
# denuvomaximum): that crash reproduced identically with exceptions
# enabled, because Z3 itself never reports an error for the call in
# question (check_error sees Z3_OK) - it's a genuine hardware exception,
# not a catchable C++ one. See doProveWithZ3/proveWithZ3Guarded in
# LLVMParser.cpp for the actual fix (a Windows SEH guard around the
# proving call). Kept anyway since it's a real, independent improvement -
# Z3 API-level errors are a distinct, more common case this now handles
# correctly instead of silently mishandling.
if(MSVC)
    # llvm-config's /EHs-c- survives as a raw passthrough flag even after
    # CMake also emits the structured ExceptionHandling=Sync property for
    # /EHsc below - MSBuild places AdditionalOptions after the structured
    # flags on the actual command line, so the leftover /EHs-c- would
    # silently win and re-disable exceptions unless explicitly removed
    # here first.
    string(REPLACE "/EHs-c-" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
else()
    string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
    # The MBA/ native port uses `xor` (and friends) as an identifier, which
    # MSVC accepts but clang/gcc reject (they are C++ alternative-token
    # keywords). Disable the operator-name synonyms so the port compiles on
    # non-MSVC toolchains. No source uses these tokens as operators.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-operator-names")
endif()

if(LLVM_LIBS)
    string(STRIP ${LLVM_LIBS} LLVM_LIBS)
endif()
if(SYS_LIBS)
    string(STRIP ${SYS_LIBS} SYS_LIBS)
endif()
if(LDF)
    string(STRIP ${LDF} LDF)
endif()

set(LLVM_LIBS ${LLVM_LIBS} ${SYS_LIBS})
set(LLVM_LIBS ${LLVM_LIBS} ${LDF})

# z3
message(STATUS "Compiling with Z3 SMT solver")
if(WIN32)
    # CONFIG (not the default Module-then-Config search order) is required here:
    # LLVM's own cmake package ships a Module-mode FindZ3.cmake (in
    # <llvm>/lib/cmake/llvm/FindZ3.cmake, added to CMAKE_MODULE_PATH by
    # find_package(LLVM CONFIG) above) that does its own find_path/find_library
    # search and silently wins over the Z3_DIR-hinted Config package below it,
    # since plain find_package() without CONFIG tries Module mode first.
    find_package(Z3 REQUIRED CONFIG)
else()
    find_package(Z3 REQUIRED CONFIG)
endif()
message(STATUS "Z3 version: ${Z3_VERSION}")
# Z3 is linked per-target (SiMBA++ via SIMBA_LLVM_LIBS, SiMBAPass below).
# mba_cli does not link Z3. The include directories are set globally so all
# targets can #include <z3++.h> when MBA_HAS_Z3 is defined.
if(DEFINED Z3_INCLUDE_DIRS)
    include_directories(${Z3_INCLUDE_DIRS})
endif()
if(TARGET z3::libz3)
    include_directories(${Z3_CXX_INCLUDE_DIRS})
endif()

message(STATUS "Z3 include directory: " ${Z3_CXX_INCLUDE_DIRS})
message(STATUS "Z3 library: " ${Z3_LIBRARIES})
include_directories(${Z3_CXX_INCLUDE_DIRS} "include" "${CMAKE_CURRENT_SOURCE_DIR}")

# Disable some LLVM warnings for Visual Studio
if(WIN32)
    add_definitions("/wd4624" "/wd4146" "/wd4244" "/wd4267")

    # The prebuilt tree at ${BuildDir} keeps its .lib files in ${BuildDir}/lib.
    # Make sure the linker can find the plain-named LLVM libraries used below
    # (the llvm-config --libs passthrough is empty when the tree ships without
    # llvm-config.exe, e.g. the c:/libs prebuild).
    if(EXISTS "${BuildDir}/lib")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LIBPATH:${BuildDir}/lib")
    endif()
endif()

include_directories(${LLVM_INCLUDE_DIRS} "include")

#separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#add_definitions(${LLVM_DEFINITIONS_LIST})

# add llvm at end so all linkers can find it
list(APPEND PROJECT_LIBRARIES ${LLVM_LIBS})
list(APPEND PROJECT_DEFINITIONS ${LLVM_DEFINITIONS})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${LLVM_INCLUDE_DIRS})

# set CXX flags for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -pipe -fomit-frame-pointer -march=native")
endif()

# Phase 9: GAMBA native C++ port sources (namespace LSiMBA::MBA). They use
# llvm::APInt (LLVM headers from ${LLVM_INCLUDE_DIRS}) and link LLVMSupport.
set(MBA_SOURCES
    "MBA/Node.cpp" "MBA/Parser.cpp" "MBA/Batch.cpp"
    "MBA/RefineA.cpp" "MBA/RefineB.cpp" "MBA/RefineC.cpp" "MBA/RefineD.cpp"
    "MBA/Expand.cpp" "MBA/Substitute.cpp" "MBA/Bitwise.cpp"
    "MBA/Implicant.cpp" "MBA/Dnf.cpp" "MBA/BitwiseFactory.cpp"
    "MBA/LinearSimplifier.cpp" "MBA/GeneralSimplifier.cpp" "MBA/Verify.cpp"
    "MBA/MultibitSimplifier.cpp" "MBA/MultibitRefiner.cpp"
    "MBA/ConstantSubstituter.cpp"
    "MBA/SemiLinearProver.cpp" "MBA/SemiLinearProver.h")

add_executable(SiMBA++ "CSiMBA.cpp" "CSiMBA.h" "LLVMParser.cpp" "LLVMParser.h" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp" "Modulo.cpp" "Modulo.h" "Z3Prover.cpp" "Z3Prover.h" "SimplifierRouter.cpp" "SimplifierRouter.h" ${MBA_SOURCES})
target_compile_definitions(SiMBA++ PRIVATE MBA_HAS_Z3)

#set_target_properties(SiMBA++ PROPERTIES
#    COMPILE_FLAGS " -fno-rtti"
#)

# LLVM libraries needed by the SiMBA++ sources. The extra set beyond the
# original list (BitWriter/BitstreamReader/MC/ProfileData/TargetParser/Linker)
# is required by the c:/libs LLVM 19.1 prebuild, where several symbols
# (e.g. TargetRegistry::lookupTarget, IRMover) moved between components.
set(SIMBA_LLVM_LIBS
        LLVMipo
        LLVMScalarOpts
        LLVMInstCombine
        LLVMTransformUtils
        LLVMAnalysis
        LLVMTarget
        LLVMOption
        LLVMObject
        LLVMBitReader
        LLVMBitWriter
        LLVMBitstreamReader
        LLVMMC
        LLVMProfileData
        LLVMTargetParser
        LLVMLinker
        LLVMCore
        LLVMSupport
        LLVMPasses
        # Extra components required by the c:/libs LLVM 19.1 prebuild (the
        # dwarf::*/demangle/remarks/coro symbols moved out of the core libs).
        LLVMDebugInfoDWARF
        LLVMDebugInfoCodeView
        LLVMDebugInfoPDB
        LLVMCoroutines
        LLVMRemarks
        LLVMDemangle
        LLVMTextAPI
        ${Z3_LIBRARIES}
)

target_link_libraries(SiMBA++ ${SIMBA_LLVM_LIBS})

if(WIN32)
    # LLVMSupport's ErrorHandling pulls in Windows ntdll symbols (Rtl*).
    target_link_libraries(SiMBA++ ntdll)
endif()

# SiMBA++ static
add_library(LSiMBA++ STATIC  "Z3Prover.cpp" "Z3Prover.h" "LLVMParser.cpp" "LLVMParser.h" "Simplifier.h" "MBAChecker.cpp" "MBAChecker.h" "Simplifier.cpp" "ShuttingYard.cpp" "Modulo.cpp" "Modulo.h")
target_compile_definitions(LSiMBA++ PRIVATE MBA_HAS_Z3)

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
add_library(SiMBAPass SHARED Z3Prover.h Z3Prover.cpp Simplifier.h Simplifier.cpp SiMBAPass.cpp SiMBAPass.h LLVMParser.cpp LLVMParser.h Simplifier.h Simplifier.cpp ShuttingYard.cpp MBAChecker.cpp MBAChecker.h Modulo.cpp Modulo.h SimplifierRouter.cpp SimplifierRouter.h ${MBA_SOURCES})

# No rtti
set_target_properties(SiMBAPass PROPERTIES
        COMPILE_FLAGS " -fno-rtti"
)

if(APPLE)
    set(PASS_LDFLAGS "-Wl,-undefined,dynamic_lookup -Wl,-no_pie")
    target_link_libraries(SiMBAPass ${PASS_LDFLAGS})
else()
    # Linux / WIN32: link the full LLVM + Z3 set.
    target_link_libraries(SiMBAPass ${SIMBA_LLVM_LIBS})
endif()
target_compile_definitions(SiMBAPass PRIVATE MBA_HAS_Z3)

# mba_cli — lightweight standalone MBA CLI (no Z3, no full LLVM).
# Links only LLVMSupport (for llvm::APInt). proveEquivalent is a no-op
# because MBA_HAS_Z3 is not defined for this target.
add_executable(mba_cli ${MBA_SOURCES} "MBA/mba_cli.cpp")
target_include_directories(mba_cli PRIVATE "MBA" "include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mba_cli LLVMSupport)

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
