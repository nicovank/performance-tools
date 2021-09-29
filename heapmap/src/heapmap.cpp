#include "pin.H"

#include <fstream>
#include <iostream>
#include <random>
#include <stdatomic.h>
#include <unordered_map>

#define ATTRIBUTE_INLINE __attribute__((always_inline))

#define LG_PARTITION_SIZE 16
static const INT64 PARTITION_SIZE = 1 << LG_PARTITION_SIZE;

using BinMap = std::unordered_map<ADDRINT, unsigned long long>;
static std::unordered_map<THREADID, BinMap> Accesses;

static std::ofstream OutputFile;
static KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "heapmap.out", "specify output file name");

inline ATTRIBUTE_INLINE VOID OnMemoryAccess(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) {
  ADDRINT MinBin = Pointer >> LG_PARTITION_SIZE;
  ADDRINT MaxBin = (Pointer + Size) >> LG_PARTITION_SIZE;
  for (ADDRINT Bin = MinBin; Bin <= MaxBin; ++Bin) {
    ++Accesses[ThreadId][Bin];
  }
}

VOID OnMemoryRead(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(ThreadId, Pointer, Size); }
VOID OnMemoryWrite(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(ThreadId, Pointer, Size); }

VOID OnProgramEnd(INT32 Code, VOID* _) {
  OutputFile << "{" << std::endl;
  OutputFile << '\t' << "\"LG_PARTITION_SIZE\": " << LG_PARTITION_SIZE << "," << std::endl;
  OutputFile << '\t' << "\"Bins\": [";

  BinMap Totals;
  for (std::pair<THREADID, BinMap> PerThreadCounters : Accesses) {
    for (std::pair<ADDRINT, unsigned long long> Counter : PerThreadCounters.second) {
      Totals[Counter.first] += Counter.second;
    }
  }

  if (!Totals.empty()) {
    std::vector<std::pair<ADDRINT, unsigned long long>> Counters(Totals.begin(), Totals.end());
    std::sort(Counters.begin(), Counters.end(), [](auto a, auto b) { return a.first < b.first; });

    OutputFile << Counters[0].second;
    for (size_t i = 1; i < Counters.size(); ++i) { // TODO: pad with 0s when there are gaps?
      OutputFile << ", " << Counters[i].second;
    }
  }

  OutputFile << "]" << std::endl << "}" << std::endl;
}

VOID Instruction(INS Instruction, VOID* _) {
  if (INS_IsMemoryRead(Instruction) && !INS_IsStackRead(Instruction)) {
    INS_InsertCall(Instruction, IPOINT_BEFORE, (AFUNPTR) OnMemoryRead, IARG_THREAD_ID, IARG_MEMORYREAD_EA,
                   IARG_MEMORYREAD_SIZE, IARG_END);
  }

  if (INS_IsMemoryWrite(Instruction) && !INS_IsStackWrite(Instruction)) {
    INS_InsertCall(Instruction, IPOINT_BEFORE, (AFUNPTR) OnMemoryWrite, IARG_THREAD_ID, IARG_MEMORYWRITE_EA,
                   IARG_MEMORYWRITE_SIZE, IARG_END);
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
