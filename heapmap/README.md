# HeapMap

This is an [Intel Pin](https://software.intel.com/content/www/us/en/develop/articles/pin-a-dynamic-binary-instrumentation-tool.html)
based tool to generate a heatmap of memory accesses on the heap.

<p align="center">
  <img src="example.jpg" alt="HeapMap Example" width="200" />
</p>

## Instructions

 1. [Download and extract Pin](https://software.intel.com/content/www/us/en/develop/articles/pin-a-binary-instrumentation-tool-downloads.html).
    This tool has been developed and tested with Pin 3.20.
 2. Set the `PIN_ROOT` environment variable to the kit's location.
 3. `make obj-intel64/heapmap.so`
 4. `$PIN_ROOT/pin -t obj-intel64/heapmap.so -- <program>`
 5. `python3 heapmap.py`

## Notes

 -  This _should_ work fine for multi-threaded programs.
 -  As of right now, gaps between partitions are not reported. They could be small or large. This would be easy to
    implement however.
 -  `LG_PARTITION_SIZE`, which is 16 by default (for a partition size of 65536 bytes) can be adjusted at the top of
    `src/heapmap.cpp`.
