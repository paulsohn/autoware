"""Build C++ consumers with the ABI required by the pinned spconv SDK."""

load("@rules_cc//cc:defs.bzl", "cc_shared_library", "cc_test")
load("@rules_cc//cc/common:cc_shared_library_info.bzl", "CcSharedLibraryInfo")
load("@with_cfg.bzl", "with_cfg")

_PLATFORM = [Label("//:linux_x86_64_gnu13")]

spconv_cc_shared_library, _spconv_cc_shared_library = with_cfg(cc_shared_library, extra_providers = [CcSharedLibraryInfo]).set("platforms", _PLATFORM).build()
spconv_cc_test, _spconv_cc_test_internal = with_cfg(cc_test).set("platforms", _PLATFORM).build()

spconv_filegroup, _spconv_filegroup = with_cfg(native.filegroup).set("platforms", _PLATFORM).build()
