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

size_t GetNextSampleCount() {
  static std::random_device Generator;
  static std::geometric_distribution<size_t> Distribution(1.0 / SAMPLING_RATE);
  return Distribution(Generator);
}

struct AllocationData {
  size_t Size;
  std::vector<Backtrace::StackFrame> AllocationTrace;
  CLOCK::time_point AllocationTime;
};

static thread_local int Busy{0};
static thread_local long long int SamplingCount = GetNextSampleCount();

static CLOCK::time_point ProgramStartTime;
static std::atomic_bool Ready{false};
static std::ofstream OutputFile;

static std::unordered_map<void*, AllocationData> Cache;
static std::mutex CacheLock;

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
                 << "\"AllocationTrace\": " << Backtrace::ToJson(Data.second.AllocationTrace) << "}" << std::endl;
    }
  }
};

static Initialization _;

void LogObject(void* Pointer, size_t Size, CLOCK::time_point AllocationTime,
               std::vector<Backtrace::StackFrame> AllocationTrace, CLOCK::time_point FreeTime,
               std::vector<Backtrace::StackFrame> FreeTrace) {

  OutputFile << "{ "
             << "\"Address\": " << (intptr_t) Pointer << ", "
             << "\"Size\": " << Size << ", "
             << "\"AllocationTime\": "
             << std::chrono::duration<double, TIME_RATIO>(AllocationTime - ProgramStartTime).count() << ", "
             << "\"AllocationTrace\": " << Backtrace::ToJson(AllocationTrace) << ", "
             << "\"FreeTime\": " << std::chrono::duration<double, TIME_RATIO>(FreeTime - ProgramStartTime).count()
             << ", "
             << "\"FreeTrace\": " << Backtrace::ToJson(FreeTrace) << "}" << std::endl;
}

extern "C" ATTRIBUTE_EXPORT void* malloc(size_t Size) noexcept {
  static decltype(::malloc)* DefaultMalloc = (decltype(::malloc)*) dlsym(RTLD_NEXT, "malloc");

  void* Pointer = (*DefaultMalloc)(Size);

  if (Ready && !Busy) {
    ++Busy;
    SamplingCount -= Size;
    if (SamplingCount <= 0) {
      SamplingCount = GetNextSampleCount();
      std::lock_guard<std::mutex> _(CacheLock);
      Cache[Pointer] = AllocationData{Size, Backtrace::GetBacktrace(), CLOCK::now()};
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
      LogObject(Pointer, Data.Size, Data.AllocationTime, Data.AllocationTrace, CLOCK::now(), Backtrace::GetBacktrace());
    }

    --Busy;
  }

  (*DefaultFree)(Pointer);
}

extern "C" ATTRIBUTE_EXPORT void* calloc(size_t NElements, size_t Size) noexcept {
  static decltype(::calloc)* DefaultCalloc = (decltype(::calloc)*) dlsym(RTLD_NEXT, "calloc");

  void* Pointer = (*DefaultCalloc)(NElements, Size);

  if (Ready && !Busy) {
    ++Busy;
    SamplingCount -= NElements * Size;
    if (SamplingCount <= 0) {
      SamplingCount = GetNextSampleCount();
      std::lock_guard<std::mutex> _(CacheLock);
      Cache[Pointer] = AllocationData{NElements * Size, Backtrace::GetBacktrace(), CLOCK::now()};
    }
    --Busy;
  }

  return Pointer;
}

extern "C" ATTRIBUTE_EXPORT void* realloc(void* Pointer, size_t Size) noexcept {
  static decltype(::realloc)* DefaultRealloc = (decltype(::realloc)*) dlsym(RTLD_NEXT, "realloc");

  void* NewPointer = (*DefaultRealloc)(Pointer, Size);

  if (Ready && !Busy) {
    ++Busy;
    std::lock_guard<std::mutex> _(CacheLock);
    if (Cache.contains(Pointer)) {
      AllocationData Data = Cache[Pointer];
      Cache.erase(Pointer);
      LogObject(Pointer, Data.Size, Data.AllocationTime, Data.AllocationTrace, CLOCK::now(), Backtrace::GetBacktrace());
    }

    Cache.emplace(NewPointer, AllocationData{Size, Backtrace::GetBacktrace(), CLOCK::now()});
    --Busy;
  }

  return NewPointer;
}