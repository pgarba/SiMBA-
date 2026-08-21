# CTest entries for the native C++ GAMBA port (Phase 10).
#
# Include this from the top-level CMakeLists.txt (after ENABLE_TESTING()) to add
# the differential tests (C++ port vs the vendored Python GAMBA oracle) and the
# standalone MBA core build test. The differential tests require a Python
# interpreter (they compare against external/GAMBA); the core build test does not.
#
#   include(MBA/CTestFragment.cmake)

# --- Standalone MBA core build (no Python needed) ---------------------------
# Builds MBA/build/mba_cli.exe via the MSVC wrapper script.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MBA/build.ps1")
  add_test(NAME MBA_Core_Build
           COMMAND pwsh -File "${CMAKE_CURRENT_SOURCE_DIR}/MBA/build.ps1"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(MBA_Core_Build PROPERTIES TIMEOUT 600)
endif()

# --- Differential tests (require Python + vendored GAMBA) -------------------
# Runs the full differential suite (parse, node, refine, expand/factorize,
# substitution, bitwise factory, linear simplifier, general simplifier).
find_package(PythonInterp)
if(PYTHONINTERP_FOUND AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/GAMBA")
  add_test(NAME MBA_Differential_All
           COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/MBA/run_all_tests.py" "100"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(MBA_Differential_All PROPERTIES TIMEOUT 1800)

  # Individual test entries (useful for CI bisection).
  foreach(_t diff_parse diff_node diff_refine diff_phase4 diff_subst
             diff_bitwise diff_simplify diff_general)
    add_test(NAME MBA_${_t}
             COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/MBA/${_t}.py" "100"
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(MBA_${_t} PROPERTIES TIMEOUT 900)
  endforeach()
endif()
