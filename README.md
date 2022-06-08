# Virtual Memory System
Created a C Library to Implement a Virtual Memory System with a Translation Lookaside Buffer (TLB)
- Built a 2-level and a 4-level page table to support a 32-bit and 64-bit virtual address space.
- I emulated the RAM (physical memory) by allocating a large region of memory in the user-space.
- Used a virtual and physical page bitmap to keep track of free/allocated virtual/physical pages.
- Provided support for different page sizes as well as different “physical memory” sizes.
