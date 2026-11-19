// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  const char * libraries[] = {
    "libGLdispatch.so.0", "libGL.so.1", "libGLX.so.0", "libOpenGL.so.0", "libEGL.so.1"};
  void * handles[5];
  for (int i = 0; i < 5; ++i) {
    handles[i] = dlopen(libraries[i], RTLD_NOW | RTLD_LOCAL);
    if (!handles[i]) {
      fprintf(stderr, "%s: %s\n", libraries[i], dlerror());
      return 1;
    }
  }
  typedef const char * (*QueryString)(void *, int);
  QueryString query = (QueryString)dlsym(handles[4], "eglQueryString");
  const char * extensions = query ? query(NULL, 0x3055 /* EGL_EXTENSIONS */) : NULL;
  if (!extensions || !strstr(extensions, "EGL_EXT_client_extensions")) {
    fprintf(stderr, "Real EGL client dispatch did not report its extensions\n");
    return 1;
  }
  printf("EGL client extensions: %s\n", extensions);
  for (int i = 4; i >= 0; --i) {
    dlclose(handles[i]);
  }
  return 0;
}
