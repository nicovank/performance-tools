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
static BinMap Accesses;
static PIN_MUTEX AccessesLock;
static TLS_KEY TlsKey = INVALID_TLS_KEY;

static std::ofstream OutputFile;
static KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "heapmap.out", "specify output file name");

VOID OnThreadStart(THREADID ThreadId, CONTEXT* Context, INT32 Flags, VOID* _) {
  BinMap* ThreadData = new BinMap;
  // Performance improvement: reserve the BinMap with an estimate of how many bins will be needed.
  if (PIN_SetThreadData(TlsKey, ThreadData, ThreadId) == false) {
    std::cerr << "Failed PIN_SetThreadData." << std::endl;
    PIN_ExitProcess(1);
  }
}

VOID OnThreadEnd(THREADID ThreadId, const CONTEXT* Context, INT32 Code, VOID* _) {
  BinMap* ThreadData = static_cast<BinMap*>(PIN_GetThreadData(TlsKey, ThreadId));
  PIN_MutexLock(&AccessesLock);
  for (std::pair<ADDRINT, unsigned long long> Counter : *ThreadData) {
    Accesses[Counter.first] += Counter.second;
  }
  PIN_MutexUnlock(&AccessesLock);
  delete ThreadData;
}

inline ATTRIBUTE_INLINE VOID OnMemoryAccess(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) {
  BinMap* ThreadData = static_cast<BinMap*>(PIN_GetThreadData(TlsKey, ThreadId));
  ADDRINT MinBin = Pointer >> LG_PARTITION_SIZE;
  ADDRINT MaxBin = (Pointer + Size) >> LG_PARTITION_SIZE;
  for (ADDRINT Bin = MinBin; Bin <= MaxBin; ++Bin) {
    ++ThreadData->operator[](Bin);
  }
}

VOID OnMemoryRead(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(ThreadId, Pointer, Size); }
VOID OnMemoryWrite(THREADID ThreadId, ADDRINT Pointer, UINT32 Size) { OnMemoryAccess(ThreadId, Pointer, Size); }

VOID OnProgramEnd(INT32 Code, VOID* _) {
  OutputFile << "{" << std::endl;
  OutputFile << '\t' << "\"LG_PARTITION_SIZE\": " << LG_PARTITION_SIZE << "," << std::endl;
  OutputFile << '\t' << "\"Bins\": [";

  PIN_MutexLock(&AccessesLock);
  if (!Accesses.empty()) {
    std::vector<std::pair<ADDRINT, unsigned long long>> Counters(Accesses.begin(), Accesses.end());
    std::sort(Counters.begin(), Counters.end(), [](auto a, auto b) { return a.first < b.first; });

    OutputFile << Counters[0].second;
    for (size_t i = 1; i < Counters.size(); ++i) { // TODO: pad with 0s when there are gaps?
      OutputFile << ", " << Counters[i].second;
    }
  }
  PIN_MutexUnlock(&AccessesLock);

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

  if ((TlsKey = PIN_CreateThreadDataKey(nullptr)) == INVALID_TLS_KEY) {
    std::cerr << "Failed PIN_CreateThreadDataKey." << std::endl;
    PIN_ExitProcess(1);
  }

  if (!PIN_MutexInit(&AccessesLock)) {
    std::cerr << "Failed PIN_MutexInit." << std::endl;
    PIN_ExitProcess(1);
  }

  INS_AddInstrumentFunction(Instruction, nullptr);
  PIN_AddThreadStartFunction(OnThreadStart, nullptr);
  PIN_AddThreadFiniFunction(OnThreadEnd, nullptr);
  PIN_AddFiniFunction(OnProgramEnd, nullptr);

  PIN_StartProgram();
}
