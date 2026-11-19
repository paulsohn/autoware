// Count successful CUDA runtime transfers in a separate diagnostic run.
#define _GNU_SOURCE
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

cudaError_t cudaMemcpyAsync(void *dst, const void *src, size_t count,
                            enum cudaMemcpyKind kind, cudaStream_t stream) {
  __typeof__(&cudaMemcpyAsync) next = dlsym(RTLD_NEXT, __func__);
  cudaError_t result = next(dst, src, count, kind, stream);
  if (result == cudaSuccess) {
    fprintf(stderr, "copy pid=%d kind=%d bytes=%zu\n", getpid(), (int)kind, count);
  }
  return result;
}
