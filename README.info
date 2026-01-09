# JOS Kernel

A Unix-like operating system kernel developed as part of my operating systems coursework.  This project implements core OS functionality including memory management, process scheduling, and inter-process communication. 

## Overview

JOS is an instructional operating system kernel that runs on x86 architecture, originally based on MIT's 6.828 Operating Systems Engineering course materials.  This implementation builds a functional kernel from boot processes through to preemptive multitasking, and throughout building it I gained hands-on experience with low-level systems programming concepts.

## Features

### 🚀 Boot Process
- Custom UEFI-compatible bootloader
- Hardware initialization and mode switching
- Kernel loading and execution

### 💾 Memory Management
- **Physical Memory Allocator**: Page-level memory management with free page tracking
- **Virtual Memory System**:  
  - Page table management and virtual-to-physical address mapping
  - Memory Management Unit (MMU) configuration
  - Support for process isolation through address space separation

### 👤 User Environments (Processes)
- Process control block data structures for environment tracking
- ELF binary loading into user address spaces
- User/kernel mode separation and privilege level management
- System call interface for user programs
- Exception handling and fault recovery

### ⚡ Preemptive Multitasking
- **Multiprocessor Support**: SMP-aware scheduling across multiple CPUs
- **Round-Robin Scheduling**: Fair CPU time distribution among processes
- **Process Management System Calls**: 
  - Unix-style `fork()` for process creation
  - Process termination and cleanup
  - Environment status monitoring
- **Inter-Process Communication (IPC)**: Message-passing mechanisms for process coordination
- **Hardware Interrupts**: Timer-based preemption for responsive multitasking
