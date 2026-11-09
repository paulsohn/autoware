# Loaded by the embedded interpreter of pyplot_test (see ../defs.bzl).
#
# The C++ host process uses LLVM's libc++/libc++abi/libunwind, loaded globally at startup. Wheels such as matplotlib
# are built against libstdc++; without RTLD_DEEPBIND their extension modules would bind __cxa_throw and the unwinder
# to the host's LLVM runtime, so their own C++ exceptions escape uncaught ("Can not load face").
import os
import sys

sys.setdlopenflags(os.RTLD_NOW | os.RTLD_DEEPBIND)
