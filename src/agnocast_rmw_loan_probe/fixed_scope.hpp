#pragma once
#include <dlfcn.h>

#include <cstdlib>
#include <stdexcept>

struct FixedScope
{
  void * lib{};
  void (*scope)(bool){};
  FixedScope(const FixedScope &) = delete;
  FixedScope & operator=(const FixedScope &) = delete;
  FixedScope() { start(); }
  void start()
  {
    if (scope) throw std::logic_error("fixed scope already started");
    if (!std::getenv("AGNOCAST_FIXED_PROBE")) return;
    lib = dlopen("librmw_agnocast.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) throw std::runtime_error("fixed probe RMW missing");
    scope = reinterpret_cast<void (*)(bool)>(dlsym(lib, "agnocast_fixed_probe_scope"));
    if (!scope) {
      dlclose(lib);
      throw std::runtime_error("fixed probe symbol missing");
    }
    scope(true);
  }
  void stop()
  {
    if (!scope) return;
    scope(false);
    scope = nullptr;
    dlclose(lib);
  }
  ~FixedScope() { stop(); }
};
