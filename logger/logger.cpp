#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

#define ATTRIBUTE_EXPORT __attribute__((visibility("default")))

#define SAMPLING_RATE 512
#define CLOCK std::chrono::steady_clock
#define TIME_RATIO std::ratio<1, 1000>
#define OUTPUT_FILENAME "malloc.out"

#include "Backtrace.hpp"

static CLOCK::time_point ProgramStartTime;
static std::atomic_bool Ready{false};
static thread_local int Busy{0};
static std::ofstream OutputFile;

size_t GetNextSampleCount() {
  static std::random_device Generator;
  static std::geometric_distribution<size_t> Distribution(1.0 / SAMPLING_RATE);
  return Distribution(Generator);
}

struct AllocationData {
  size_t Size;
  std::vector<Backtrace::StackFrame> AllocationBacktrace;
  CLOCK::time_point AllocationTime;
};

std::unordered_map<void*, AllocationData> Cache;
std::mutex CacheLock;

class Initialization {
public:
  Initialization() {
    OutputFile.open(OUTPUT_FILENAME, std::ios_base::app);
    Backtrace::Initialize();
    ProgramStartTime = CLOCK::now();
    Ready = true;
  }

  ~Initialization() {
    Ready = false;

    for (auto Data : Cache) {
      OutputFile << "{ "
                 << "\"Address\": " << (intptr_t) Data.first << ", "
                 << "\"Size\": " << Data.second.Size << ", "
                 << "\"AllocationTime\": "
                 << std::chrono::duration<double, TIME_RATIO>(Data.second.AllocationTime - ProgramStartTime).count()
                 << ", "
                 << "\"AllocationBacktrace\": " << Backtrace::ToJson(Data.second.AllocationBacktrace) << "}"
                 << std::endl;
    }
  }
};

static Initialization _;

extern "C" ATTRIBUTE_EXPORT void* malloc(size_t Size) noexcept {
  static thread_local long int SamplingCount = GetNextSampleCount();
  static decltype(::malloc)* DefaultMalloc = (decltype(::malloc)*) dlsym(RTLD_NEXT, "malloc");

  void* Pointer = (*DefaultMalloc)(Size);

  if (Ready && !Busy) {
    ++Busy;
    SamplingCount -= Size;
    if (SamplingCount <= 0) {
      SamplingCount = GetNextSampleCount();
      std::lock_guard<std::mutex> _(CacheLock);
      Cache.emplace(Pointer, AllocationData{Size, Backtrace::GetBacktrace(), CLOCK::now()});
    }
    --Busy;
  }

  return Pointer;
}

extern "C" ATTRIBUTE_EXPORT void free(void* Pointer) {
  static decltype(::free)* DefaultFree = (decltype(::free)*) dlsym(RTLD_NEXT, "free");

  if (Ready && !Busy && Pointer != nullptr) {
    ++Busy;

    std::lock_guard<std::mutex> _(CacheLock);
    if (Cache.contains(Pointer)) {
      AllocationData Data = Cache[Pointer];
      Cache.erase(Pointer);
      OutputFile << "{ "
                 << "\"Address\": " << (intptr_t) Pointer << ", "
                 << "\"Size\": " << Data.Size << ", "
                 << "\"AllocationTime\": "
                 << std::chrono::duration<double, TIME_RATIO>(Data.AllocationTime - ProgramStartTime).count() << ", "
                 << "\"AllocationBacktrace\": " << Backtrace::ToJson(Data.AllocationBacktrace) << ", "
                 << "\"FreeTime\": "
                 << std::chrono::duration<double, TIME_RATIO>(CLOCK::now() - ProgramStartTime).count() << ", "
                 << "\"FreeBacktrace\": " << Backtrace::ToJson(Backtrace::GetBacktrace()) << "}" << std::endl;
    }

    --Busy;
  }

  (*DefaultFree)(Pointer);
}