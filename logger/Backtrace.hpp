#ifndef BACKTRACE_HPP
#define BACKTRACE_HPP

#include <backtrace.h>
#include <cxxabi.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace Backtrace {
namespace {
static backtrace_state* State = nullptr;
}

struct StackFrame {
  uintptr_t ProgramCounter;
  std::string Function;
  std::string Filename;
  int LineNo;
};

std::string ToJson(StackFrame Frame) {
  return "{ \"Function\": \"" + Frame.Function + "\", \"Filename\": \"" + Frame.Filename +
         "\", \"LineNo\": " + std::to_string(Frame.LineNo) + " }";
}

std::string ToJson(std::vector<StackFrame> Trace) {
  if (Trace.empty()) {
    return "[]";
  }

  std::stringstream OutputStream;
  OutputStream << "[" << ToJson(Trace[0]);
  for (int i = 1; i < Trace.size(); ++i) {
    OutputStream << ", " << ToJson(Trace[i]);
  }
  OutputStream << "]";
  return OutputStream.str();
}

void Initialize(char* Filename) {
  if (State == nullptr) {
    const auto OnError = [](void*, const char* Message, int Error) {
      std::cerr << "Error #" << Error << " while setting up backtrace. Message: " << Message << std::endl;
    };

    State = backtrace_create_state(Filename, true, OnError, nullptr);
  }
}

void Initialize() { Initialize(nullptr); }

std::vector<StackFrame> GetBacktrace(int Skip) {
  if (State == nullptr) {
    return std::vector<StackFrame>();
  }

  const auto OnError = [](void*, const char* Message, int Error) {
    std::cerr << "Error #" << Error << " while getting backtrace. Message: " << Message << std::endl;
  };

  const auto OnStackFrame = [](void* Data, uintptr_t ProgramCounter, const char* Filename, int LineNo,
                               const char* Function) {
    auto* Backtrace = static_cast<std::vector<StackFrame>*>(Data);

    StackFrame Frame;

    if (!Function && !Filename) {
      return 0;
    }

    if (!Function) {
      Frame.Function = "[UNKNOWN]";
    } else {
      int Status;
      char* Demangled = abi::__cxa_demangle(Function, nullptr, nullptr, &Status);
      if (Status == 0) {
        Frame.Function = std::string(Demangled);
        static decltype(::free)* DefaultFree = (decltype(::free)*) dlsym(RTLD_DEFAULT, "free");
        (*DefaultFree)(Demangled);
      } else {
        Frame.Function = std::string(Function);
      }
    }

    Frame.Filename = (!Filename) ? "[UNKNOWN]" : std::string(Filename);
    Frame.LineNo = LineNo;
    Frame.ProgramCounter = ProgramCounter;

    Backtrace->insert(Backtrace->begin(), Frame);

    return 0;
  };

  std::vector<StackFrame> Backtrace;
  backtrace_full(State, Skip + 1, OnStackFrame, OnError, &Backtrace);
  return Backtrace;
}

std::vector<StackFrame> GetBacktrace() { return GetBacktrace(1); }
} // namespace Backtrace

#endif // BACKTRACE_HPP