#include <atomic>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <vector>

#define ATTRIBUTE_EXPORT __attribute__((visibility("default")))

#include "Backtrace.hpp"

static std::atomic_bool Ready{false};
static thread_local int Busy{0};

class Initialization {
public:
  Initialization() {
    Backtrace::Initialize();
    Ready = true;
  }

  ~Initialization() { Ready = false; }
};

static Initialization _;

extern "C" ATTRIBUTE_EXPORT void* malloc(size_t Size) noexcept {
  static decltype(::malloc)* DefaultMalloc = (decltype(::malloc)*) dlsym(RTLD_NEXT, "malloc");

  void* Pointer = (*DefaultMalloc)(Size);

  if (Ready && !Busy) {
    ++Busy;

    --Busy;
  }

  return Pointer;
}

extern "C" ATTRIBUTE_EXPORT void free(void* Pointer) {
  static decltype(::free)* DefaultFree = (decltype(::free)*) dlsym(RTLD_NEXT, "free");

  if (Ready && !Busy && Pointer != nullptr) {
    ++Busy;

    --Busy;
  }

  (*DefaultFree)(Pointer);
}