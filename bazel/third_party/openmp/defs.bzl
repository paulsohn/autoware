"""Compile flags for code using OpenMP (CMake's OpenMP::OpenMP_CXX INTERFACE_COMPILE_OPTIONS)."""

# -fopenmp only at compile time: at link time clang would add -lomp; :openmp provides the runtime instead.
OPENMP_COPTS = ["-fopenmp"]
