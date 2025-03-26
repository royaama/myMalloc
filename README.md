Custom Memory Allocator  
A custom implementation of dynamic memory allocation in C++, mimicking the behavior of `malloc`, `calloc`, `free`, and `realloc`. This allocator efficiently manages memory using **buddy allocation** and **mmap** for large allocations.  
 Features :
- Dynamic Allocation Functions: Implements `smalloc`, `scalloc`, `sfree`, and `srealloc`.  
- Buddy System Memory Management: Efficient memory splitting and merging.  
- Metadata Tracking: Keeps track of allocated and free memory blocks.  
- Efficient Heap Management: Uses `sbrk` and `mmap` for optimized memory allocation.  
 Installation&Usage:
Compiling  
g++ -o memory_allocator malloc_3.cpp 
