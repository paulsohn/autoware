// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <dlfcn.h>
#include <stdio.h>

int main(void)
{
  void * x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  void * glx = dlopen("libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
  if (!x11 || !glx) {
    fprintf(stderr, "Cannot load X11/GLX: %s\n", dlerror());
    return 1;
  }
  void * (*open_display)(const char *) = dlsym(x11, "XOpenDisplay");
  int (*default_screen)(void *) = dlsym(x11, "XDefaultScreen");
  int (*free_visual)(void *) = dlsym(x11, "XFree");
  int (*close_display)(void *) = dlsym(x11, "XCloseDisplay");
  void * (*choose_visual)(void *, int, int *) = dlsym(glx, "glXChooseVisual");
  const char * (*client_string)(void *, int) = dlsym(glx, "glXGetClientString");
  if (
    !open_display || !default_screen || !free_visual || !close_display || !choose_visual ||
    !client_string) {
    fprintf(stderr, "Missing X11/GLX API\n");
    return 1;
  }
  void * display = open_display(NULL);
  if (!display) {
    fprintf(stderr, "Set DISPLAY and XAUTHORITY to an accessible X11 display\n");
    return 1;
  }
  // Match RViz's dummy visual: RGBA, double buffer, depth >=16, stencil >=8.
  int attributes[] = {4, 5, 12, 16, 13, 8, 0};
  void * visual = choose_visual(display, default_screen(display), attributes);
  const char * vendor = client_string(display, 1 /* GLX_VENDOR */);
  if (!visual || !vendor) {
    fprintf(stderr, "No GLX visual/vendor: check the selected driver's native ABI closure\n");
    close_display(display);
    return 1;
  }
  printf("GLX client vendor: %s; RViz-compatible visual found\n", vendor);
  free_visual(visual);
  close_display(display);
  dlclose(glx);
  dlclose(x11);
  return 0;
}
