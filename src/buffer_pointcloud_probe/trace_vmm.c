// Optional diagnostic for the native CUDA case. Report successful VMM IPC calls.
#define _GNU_SOURCE
#include <cuda.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

CUresult cuMemExportToShareableHandle(void *out, CUmemGenericAllocationHandle allocation,
                                      CUmemAllocationHandleType kind, unsigned long long flags) {
  __typeof__(&cuMemExportToShareableHandle) next = dlsym(RTLD_NEXT, __func__);
  CUresult result = next(out, allocation, kind, flags);
  if (result == CUDA_SUCCESS) fprintf(stderr, "VMM export pid=%d\n", getpid());
  return result;
}

CUresult cuMemImportFromShareableHandle(CUmemGenericAllocationHandle *out, void *handle,
                                        CUmemAllocationHandleType kind) {
  __typeof__(&cuMemImportFromShareableHandle) next = dlsym(RTLD_NEXT, __func__);
  CUresult result = next(out, handle, kind);
  if (result == CUDA_SUCCESS) fprintf(stderr, "VMM import pid=%d\n", getpid());
  return result;
}
