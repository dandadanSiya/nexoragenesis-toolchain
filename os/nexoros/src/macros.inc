// Effect and clobber sets shared by kernel functions. Every routine may run
// with interrupts and touch the hardware, so they all declare the maximal
// set; the NTASM verifier still checks each instruction against it.
%text FX effects {reads_mem,writes_mem,changes_flags,privileged,io_port,reads_clock,interrupt_state,suspends,control,memory_ordering}
%text CLOB clobbers {rax,rbx,rcx,rdx,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rflags}
