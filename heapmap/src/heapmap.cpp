#include "pin.H"

#include <fstream>
#include <iostream>
#include <random>
#include <stdatomic.h>
#include <unordered_map>

#define ATTRIBUTE_INLINE __attribute__((always_inline))

#define LG_PARTITION_SIZE 8

static const INT64 PARTITION_SIZE = 1 << LG_PARTITION_SIZE;

using counter_t = unsigned long long;
static std::unordered_map<ADDRINT, counter_t> Accesses; // TODO: Thread-local counters, add em up at the end.

static std::ofstream OutputFile;
static KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "heapmap.out", "specify output file name");

inline ATTRIBUTE_INLINE VOID OnMemoryAccess(ADDRINT Pointer, UINT32 Size) {
  ADDRINT MinBin = Pointer >> LG_PARTITION_SIZE;
  ADDRINT MaxBin = (Pointer + Size) >> LG_PARTITION_SIZE;
  for (ADDRINT Bin = MinBin; Bin <= MaxBin; ++Bin) {
    ++Accesses[Bin];
  }
}

VOID OnMemoryRead(ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(Pointer, Size); }
VOID OnMemoryWrite(ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(Pointer, Size); }

VOID OnProgramEnd(INT32 Code, VOID* _) {
  OutputFile << "LG_PARTITION_SIZE " << LG_PARTITION_SIZE << std::endl;

  std::vector<std::pair<ADDRINT, counter_t>> Counters(Accesses.begin(), Accesses.end());
  std::sort(Counters.begin(), Counters.end(), [](auto a, auto b) { return a.first < b.first; });

  for (auto Counter : Counters) { // TODO: pad with 0s when there are gaps?
    OutputFile << Counter.second << std::endl;
  }
}

VOID Instruction(INS Instruction, VOID* _) {
  if (INS_IsMemoryRead(Instruction) && !INS_IsStackRead(Instruction)) {
    INS_InsertCall(Instruction, IPOINT_BEFORE, (AFUNPTR) OnMemoryRead, IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
                   IARG_END);
  }

  if (INS_IsMemoryWrite(Instruction) && !INS_IsStackWrite(Instruction)) {
    INS_InsertCall(Instruction, IPOINT_BEFORE, (AFUNPTR) OnMemoryWrite, IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
                   IARG_END);
  }
}

INT32 Usage() {
  std::cerr << "HeapMap: Generate a heatmap of heap memory accesses." << std::endl;
  std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
  return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
  PIN_InitSymbols();
  if (PIN_Init(argc, argv)) {
    return Usage();
  }

  OutputFile.open(KnobOutputFile.Value().c_str());

  INS_AddInstrumentFunction(Instruction, nullptr);
  PIN_AddFiniFunction(OnProgramEnd, nullptr);

  PIN_StartProgram();
}
