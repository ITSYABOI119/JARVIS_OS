# BLIND QUALITY JUDGING SET — JARVIS control-IN workload

Four anonymised systems answered the same 12 questions. Identities are withheld.

Each system has a FIXED label ([SYS-1]..[SYS-4]) that means the SAME system on
every question, so per-system scoring is valid. The ORDER the four appear in is
shuffled per question, so POSITION carries no information — only the label does.


========================================================================
## Q1. explain paging in one line
========================================================================


[SYS-4]
**Paging is a memory management technique that divides physical memory into fixed-size blocks called pages and logical memory into similarly sized pages to efficiently manage and access memory.**


[SYS-1]
**Paging is a memory management technique that divides both physical memory and logical memory into fixed-size blocks to allow efficient allocation and protection.**


[SYS-2]
Paging is a memory management technique that divides physical and logical memory into fixed-size blocks to allow non-contiguous memory allocation and efficient swapping.


[SYS-3]
Paging is a memory management technique where a large memory space is divided into smaller, fixed-size blocks called pages, allowing for efficient allocation and deallocation of memory.


========================================================================
## Q2. in one line, what is a mutex
========================================================================


[SYS-4]
A mutex (mutual exclusion) is a synchronization primitive that ensures only one thread can access a shared resource at a time.


[SYS-2]
A mutex is a synchronization primitive that ensures only one thread can access a shared resource at any given time, preventing race conditions.


[SYS-3]
A mutex (short for "mutual exclusion") is a synchronization mechanism that allows only one thread to access a shared resource at a time, preventing data corruption and ensuring thread safety.


[SYS-1]
A mutex (mutual exclusion) is a synchronization primitive used to protect shared resources from being accessed by multiple threads simultaneously.


========================================================================
## Q3. in one line, what is a translation lookaside buffer
========================================================================


[SYS-1]
A Translation Lookaside Buffer (TLB) is a small, fast cache used by the CPU to speed up virtual-to-physical address translation by storing recent mappings.


[SYS-2]
A Translation Lookaside Buffer (TLB) is a small, fast cache that stores recent virtual-to-physical address translations to speed up memory access.


[SYS-4]
A Translation Lookaside Buffer (TLB) is a high-speed cache that stores recent virtual-to-physical address translations to speed up memory access.


[SYS-3]
A Translation Lookaside Buffer (TLB) is a cache memory that stores recent translations of virtual addresses to physical addresses, reducing the time it takes for a CPU to access memory.


========================================================================
## Q4. in one line, what is a memory management unit
========================================================================


[SYS-1]
A Memory Management Unit (MMU) is a hardware component that handles the translation of virtual memory addresses to physical memory addresses.


[SYS-3]
A Memory Management Unit (MMU) is a hardware component that translates virtual memory addresses to physical memory addresses, enabling efficient and secure memory management in computer systems.


[SYS-2]
A Memory Management Unit (MMU) is a hardware component that handles the translation of virtual memory addresses used by a CPU into the physical memory addresses used by RAM.


[SYS-4]
A Memory Management Unit (MMU) is a hardware component that handles the translation of virtual memory addresses to physical memory addresses.


========================================================================
## Q5. in one line, what does the seL4 capability system provide
========================================================================


[SYS-2]
The seL4 capability system provides fine-grained, secure control over system resources by enforcing access rights through unforgeable tokens called capabilities.


[SYS-1]
The seL4 capability system provides a **formal, mathematically verified foundation for secure and fine-grained isolation and access control** in operating systems and critical systems.


[SYS-3]
The seL4 capability system provides a formal, provably correct, and highly secure mechanism for enforcing access control and memory protection in operating systems.


[SYS-4]
The seL4 capability system provides a **formal verification foundation for secure, isolated, and trustworthy systems by strictly controlling resource access.**


========================================================================
## Q6. what is a page fault?
========================================================================


[SYS-4]
A **page fault** is a type of interrupt that occurs in a **virtual memory system** when a program tries to access a page of memory that is **not currently loaded into physical RAM (main memory)**.
In simpler terms, it's the operating system's way of saying: **"Wait a minute, the data you are asking for isn't in the fast memory (RAM); I have to go get it from the slower storage (like the hard drive or SSD)."**
Here is a detailed breakdown of what causes it, how it works, and why it's important:
## 1. The Context: Virtual Memory
To understand a page fault, you first need to understand **Virtual Memory**:
* **Virtual Address Space:** Every running program operates within its own private, large, contiguous address space (virtual addresses). This space is an abstraction that the program sees.
* **Physical Memory (RAM):** The actual hardware memory chips in the computer.
* **Paging:** The operating system divides both the virtual address space and the physical memory into fixed-size blocks called **pages** (for virtual memory) and **frames** (for physical memory).
The goal of


[SYS-2]
## What is a Page Fault?
A **page fault** is a specific type of hardware interrupt that occurs when a running program tries to access a piece of data or an instruction that is currently **not loaded into the computer's main memory (RAM)**, but is instead stored on a secondary storage device (like a hard drive or SSD).
In simpler terms, the operating system (OS) is managing the program's memory using a technique called **virtual memory**. When the program asks for a memory address, the hardware checks if the required data is in RAM. If it's not, the hardware triggers a page fault, signaling the OS that it needs to intervene.
### 🧠 The Concept: Virtual Memory and Paging
To understand a page fault, you first need to understand **virtual memory**:
1. **Virtual Address Space:** Every running program believes it has access to a large, contiguous block of memory (its *virtual address space*).
2. **Physical Memory (RAM):** The actual physical memory available on the computer is finite.
3. **Paging:** The OS divides both the program's virtual memory and the physical RAM into fixed-size blocks called **pages**


[SYS-3]
A page fault is an event that occurs when a program attempts to access a memory page that is not currently in its memory cache. This can happen for several reasons:
1. **Page not loaded**: The operating system may not have loaded the page into memory yet, or it may have been swapped out to disk due to memory constraints.
2. **Page fault due to protection**: The operating system may have set permissions on the page to prevent access, such as read-only or no access.
3. **Page fault due to invalid address**: The program may have attempted to access a memory address that is not valid or is outside the program's memory space.
When a page fault occurs, the operating system takes the following steps:
1. **Trap**: The operating system generates a trap or interrupt to handle the page fault.
2. **Check the page table**: The operating system checks the page table to determine the location of the page on disk.
3. **Load the page**: The operating system loads the page from disk into memory.
4. **Update the page table**: The operating system updates the page table to reflect the new location of the page in memory.
5. **Resume execution**: The operating system resumes execution of the program, allowing it


[SYS-1]
A **page fault** is a type of interrupt that occurs in a **virtual memory system** when a program tries to access a page of memory that is **not currently loaded into physical RAM (main memory)**.
In simpler terms, it's the operating system's way of handling the situation where the computer needs data from the hard drive (or SSD) to complete a memory access, but that data isn't physically in the fast RAM chips right now.
Here is a detailed breakdown of what causes it, how it works, and why it's important.
## 1. The Context: Virtual Memory
To understand a page fault, you first need to understand **virtual memory**:
* **Virtual Address Space:** Every running program (process) is given the illusion that it has access to a very large, contiguous block of memory. This is the **virtual address space**.
* **Physical Memory (RAM):** The actual physical memory chips installed in the computer are limited.
* **Paging:** The operating system divides both the virtual address space and the physical memory into fixed-size blocks called **pages** (for virtual memory) and **frames** (for physical memory).
The goal


========================================================================
## Q7. why doesn't adding more CPU cores speed up a single-threaded program?
========================================================================


[SYS-4]
This is a fundamental concept in computer architecture and parallel programming. Here's a detailed breakdown of why adding more CPU cores **does not** speed up a purely single-threaded program:
## 1. The Bottleneck: Sequential Execution
A **single-threaded program** is one that executes instructions one after the other, in a strict sequence. Think of it like a single cashier at a grocery store: they can only process one customer's transaction completely before they can start the next one.
* **The Process:** The program runs on one CPU core. It starts at instruction A, finishes it, then moves to instruction B, finishes it, and so on.
* **The Limitation:** The speed of the entire program is determined by the time it takes to execute all those instructions sequentially. This time is dictated by the *latency* and *throughput* of that **single core**.
**Analogy:** If you have a single highway lane (one core), adding more parallel lanes (more cores) doesn't make a single car travel faster on that one lane; it just means you can handle more cars simultaneously.
## 2. The Core Limitation: The Single Thread
Since the program is single


[SYS-1]
This is a fundamental concept in computer architecture and parallel programming. The reason adding more CPU cores **doesn't speed up a single-threaded program** is because of **how a single-threaded program executes** and the **nature of parallelism**.
Here is a detailed breakdown of why this is the case:
## 1. The Bottleneck: The Single Thread
A single-threaded program executes its instructions sequentially, one after the other, on a single CPU core.
* **Sequential Execution:** Imagine a chef (the CPU core) making a complex meal (the program). They must complete Step 1 entirely before starting Step 2, and so on.
* **The Limiting Factor:** The speed of the program is limited by the time it takes for that single core to execute all the instructions. If the program is CPU-bound (meaning it spends most of its time performing calculations), the speed is directly proportional to the clock speed and efficiency of that single core.
**Analogy:** If you have one worker trying to build a wall, adding more workers won't make the *single* worker build the wall faster; it just means you have more people available to build *multiple* walls simultaneously.


[SYS-2]
This is a fundamental concept in computer architecture and operating systems. The short answer is: **A single-threaded program is inherently limited by the speed of a single execution path, and adding more cores doesn't change the speed of that single path.**
Here is a detailed breakdown of why this is the case, using analogies and technical concepts:
## 1. The Concept of a "Thread"
To understand this, you must first understand what a thread is:
* **A Thread is a Sequence of Instructions:** A thread is the smallest sequence of programmed instructions that can be managed independently by a scheduler. Think of it as a single worker or a single lane on a highway.
* **Single-Threaded Program:** A program that is single-threaded has only **one** worker. It must execute all its instructions sequentially, one after the other. It cannot do Task A and Task B at the exact same moment.
## 2. The Role of CPU Cores
* **A Core is an Execution Unit:** A CPU core is a physical processing unit capable of fetching, decoding, and executing instructions.
* **Parallelism:** Multiple cores allow a computer to perform **parallelism**—doing multiple


[SYS-3]
Adding more CPU cores can speed up multi-threaded programs, but not single-threaded programs. Here's why:
1. **Single-threaded programs only use one core**: A single-threaded program executes instructions sequentially, one at a time. It doesn't have the ability to take advantage of multiple cores, as it's only using one core at a time.
2. **No parallelism**: Single-threaded programs don't have multiple threads that can run concurrently on different cores. As a result, adding more cores doesn't provide any additional processing power for the program.
3. **No thread-level parallelism**: Even if a single-threaded program uses multiple threads (e.g., for I/O operations), the threads are still executed sequentially on a single core. The program doesn't take advantage of multiple cores to execute threads in parallel.
4. **No hyper-threading benefits**: Hyper-threading (also known as simultaneous multithreading) allows a single core to execute multiple threads concurrently. However, single-threaded programs don't benefit from hyper-threading, as they only use one thread.
To speed up a single-threaded program, you can consider the following options:
1. **Optimize the program**: Improve the program's performance


========================================================================
## Q8. In one sentence, why doesn't adding more CPU cores speed up a single-threaded program?
========================================================================


[SYS-3]
Adding more CPU cores does not speed up a single-threaded program because the program can only utilize one core at a time, and the additional cores remain idle until the program is modified to take advantage of multi-threading or parallel processing.


[SYS-4]
A single-threaded program is bottlenecked by the speed of the single processor core, so adding more cores will not increase its execution speed.


[SYS-2]
A single-threaded program can only execute instructions on one core at a time, so adding more cores provides no benefit because the workload is inherently limited to a single processing thread.


[SYS-1]
Adding more CPU cores does not speed up a single-threaded program because that program is inherently limited by the speed of a single processor core.


========================================================================
## Q9. Explain how virtual memory works in three sentences.
========================================================================


[SYS-1]
Virtual memory allows a computer to run programs larger than the physical RAM by using disk space as an extension of memory. It creates an illusion for each process, giving it a contiguous, large address space regardless of physical memory availability. This system efficiently manages memory by swapping inactive data between RAM and the hard drive as needed.


[SYS-4]
Virtual memory allows a system to run programs larger than the physical RAM by using disk space as an extension of memory. It creates an illusion for each process that it has access to a large, contiguous block of memory, even if the physical memory is fragmented. When a program needs data not in RAM, the operating system swaps less-used pages to disk and brings the required pages back into physical memory.


[SYS-2]
Virtual memory is a memory management technique that allows a computer to run programs larger than the physical RAM available by using a portion of the hard drive as an extension of memory. When the system needs to access data not currently in RAM, it swaps "pages" of data between fast RAM and slower disk storage, a process called paging. This abstraction provides each process with the illusion of having a large, contiguous block of private memory, simplifying programming and improving multitasking.


[SYS-3]
Virtual memory is a combination of physical RAM (Random Access Memory) and a storage device, such as a hard drive, that acts as an extension of the RAM to provide more memory space for running applications. When the physical RAM is full, the operating system moves less frequently used data from RAM to the storage device, freeing up space in RAM for more important data. This process, called paging or swapping, allows the system to use more memory than is physically available, making it appear as if the system has more RAM than it actually does.


========================================================================
## Q10. What is the difference between a process and a thread?
========================================================================


[SYS-4]
The difference between a **process** and a **thread** is fundamental to understanding how modern operating systems manage and execute programs. The core distinction lies in **what they are**, **how they share resources**, and **how they are isolated**.
Here is a detailed breakdown of the differences:
## 1. Process (The Program Instance)
A **process** is an independent, running instance of a program. It is the operating system's mechanism for executing a program.
### Key Characteristics of a Process:
* **Independent Execution:** A process has its own dedicated memory space, resources (files, I/O devices), and execution context.
* **Heavyweight:** Creating a process requires significant overhead because the OS must allocate a large, separate memory space for it.
* **Isolation:** Processes are isolated from each other. If one process crashes, it generally does not affect other processes running on the system.
* **Communication:** Communication between different processes (Inter-Process Communication or IPC) is relatively complex and requires specific mechanisms (like pipes, sockets, shared memory).
* **Context Switching:** Switching between processes (context switching) is relatively slow because the OS has to save the entire state (memory


[SYS-2]
The difference between a **process** and a **thread** is fundamental to how modern operating systems manage and execute programs. In simple terms, you can think of a **process as a running program**, and **threads as the individual workers *within* that program**.
Here is a detailed breakdown of the differences, similarities, and when you would use each.
## 🧠 Process (The Program Container)
A **process** is an independent instance of a running program. When you double-click an application icon (like Chrome or Word), the operating system creates a new process for that application.
### Key Characteristics of a Process:
1. **Independent Memory Space:** Each process has its own dedicated, isolated memory space (its own heap, stack, and data segments). This is the most critical difference. If one process crashes, it generally does not affect other processes.
2. **Resource Ownership:** A process owns system resources, such as file handles, memory addresses, and security credentials.
3. **Heavyweight:** Creating a process is an "expensive" or "heavyweight" operation because the OS has to allocate and set up a completely new, isolated environment for it.
4. **Inter-


[SYS-3]
In computer science, both processes and threads are used to execute multiple tasks concurrently, but they have distinct differences in terms of their execution, memory management, and resource utilization.
**Process:**
A process is an independent program that is running on a computer. Each process has its own:
1. **Memory space**: Each process has its own private memory space, which means that changes made by one process do not affect other processes.
2. **Resources**: Each process has its own set of resources, such as open files, network connections, and system calls.
3. **Program counter**: Each process has its own program counter, which keeps track of the current instruction being executed.
4. **Stack**: Each process has its own stack, which stores function call information and local variables.
When a process is created, a new memory space is allocated for it, and the operating system manages the resources and memory for that process.
**Thread:**
A thread is a lightweight process that shares the same memory space as its parent process. Threads are often used to improve responsiveness and efficiency in applications that require concurrent execution.
1. **Shared memory space**: Threads share the same memory space as their parent process, which means that changes made by one thread can affect


[SYS-1]
The concepts of **process** and **thread** are fundamental to modern operating systems and concurrent programming. While both are mechanisms for executing tasks, they differ significantly in terms of **resource ownership, isolation, and communication.**
Here is a detailed breakdown of the differences between a process and a thread.
## 1. Process (The Program Instance)
A **process** is an independent execution environment created by the operating system (OS) to run a program. It is the fundamental unit of resource allocation.
### Key Characteristics of a Process:
* **Resource Ownership:** A process owns its own dedicated set of resources, including:
* **Memory Space:** Its own isolated virtual address space.
* **File Handles:** Access to the files it needs.
* **System Resources:** CPU time allocation, I/O devices, etc.
* **Isolation (Independence):** Processes are highly isolated from one another. One process cannot directly access the memory of another process without explicit mechanisms (like Inter-Process Communication or IPC).
* **Heavyweight:** Creating a new process is a relatively **expensive** operation because the OS must allocate a completely new memory space, file descriptors, and process control block (


========================================================================
## Q11. Describe the TCP three-way handshake step by step.
========================================================================


[SYS-2]
The TCP (Transmission Control Protocol) three-way handshake is the fundamental process used to establish a reliable connection between a client (the initiator) and a server (the listener) before any actual data transfer can occur. It ensures that both sides are ready, synchronized, and agree on the initial sequence numbers for the communication.
Here is a step-by-step breakdown of the three-way handshake:
## The Three Steps
The handshake involves three distinct packets exchanged between the client and the server: **SYN**, **SYN-ACK**, and **ACK**.
### Step 1: SYN (Synchronize) - Client $\rightarrow$ Server
The client initiates the connection by sending a **SYN (Synchronize)** segment to the server.
* **What it does:** The client is essentially saying, "Hello, I want to start a connection. I am ready to communicate, and here is my initial sequence number."
* **Key Information:** The client includes its initial sequence number, denoted as **$X$**.
* **Analogy:** The client knocks on the server's door and says, "Are you there? I'd like to talk."
### Step 2: SYN-ACK (


[SYS-1]
The **TCP Three-Way Handshake** is a fundamental process used to establish a reliable connection between a client (initiator) and a server on the Internet Protocol (IP) network before any actual data is exchanged. It ensures that both parties are ready to communicate and agree on initial sequence numbers.
Here is a step-by-step breakdown of the process:
## The Three Steps of the TCP Three-Way Handshake
The handshake involves three distinct steps, each involving the exchange of specific TCP segments (packets).
### Step 1: SYN (Synchronize) - Client to Server
The process begins when the **Client** wants to initiate a connection to the **Server**.
* **Action:** The Client sends a TCP segment to the Server with the **SYN flag** set to 1.
* **Purpose:** This segment serves two main purposes:
1. **Synchronization:** It informs the server that the client wishes to establish a connection and synchronize its initial sequence number (ISN).
2. **Request:** It signals the intent to start a connection.
* **Analogy:** The client is saying, "Hello, I want to talk. Here is my starting number."


[SYS-3]
The TCP three-way handshake, also known as the TCP connection establishment process, is a process used to establish a connection between a client and a server over a network. Here's a step-by-step explanation of the process:
**Step 1: SYN (Synchronize) Packet from Client**
1. The client, which is the device initiating the connection, sends a SYN packet to the server. This packet contains the client's initial sequence number (ISN) and other control information.
2. The SYN packet is sent to the server's IP address and port number, which is specified in the client's application layer protocol (e.g., HTTP, FTP, etc.).
**Step 2: SYN-ACK (Synchronize-Acknowledgment) Packet from Server**
1. Upon receiving the SYN packet, the server responds with a SYN-ACK packet. This packet contains the server's ISN and an acknowledgment of the client's ISN.
2. The SYN-ACK packet is sent back to the client's IP address and port number.
**Step 3: ACK (Acknowledgment) Packet from Client**
1. The client receives the SYN-ACK packet and sends an ACK packet back to the server. This packet acknowledges the server's


[SYS-4]
The **TCP Three-Way Handshake** is a fundamental process used to establish a reliable connection between a client (initiator) and a server on the Internet. It ensures that both parties are ready to communicate, agree on initial sequence numbers, and synchronize their states before any actual data transmission begins.
Here is a step-by-step description of the TCP three-way handshake:
### Prerequisites
Before the handshake begins, the client and server must be prepared to communicate over the chosen port numbers.
### Step 1: SYN (Synchronize) - Client to Server
The process starts when the **Client** wants to initiate a connection with the **Server**.
* **Action:** The Client sends a TCP segment to the server with the **SYN flag** set to 1.
* **Purpose:** This segment signals the desire to establish a connection and proposes an initial sequence number ($\text{Client Sequence Number}$, often denoted as $X$).
* **Analogy:** The client is saying, "Hello, I want to talk to you. If you're willing, I'll start counting from number $X$."
### Step 2: SYN-ACK (Synchronize-Acknowledge


========================================================================
## Q12. Write a C function that reverses a string in place.
========================================================================


[SYS-1]
Here is a C function that reverses a string in place.
This implementation uses two pointers, one starting at the beginning and one at the end of the string, and swaps the characters they point to until they meet in the middle.
### C Implementation
```c
#include <stdio.h>
#include <string.h>
**
* reverseStringInPlace
* 
* Reverses the characters of a null-terminated string in place.
* 
* @param str The string (character array) to be reversed.
*/
void reverseStringInPlace(char *str) {
if (str == NULL) {
Handle the case where a NULL pointer is passed
return;
}
Get the length of the string
int length = strlen(str);
Initialize two pointers: one at the start and one at the end
int start = 0;
int end = length - 1;
Temporary character to hold the swapped value
char temp;
Loop until the pointers meet or cross each other
while (start < end) {


[SYS-4]
Here is a C function that reverses a string in place.
This implementation uses two pointers, one starting at the beginning and one at the end of the string, and swaps the characters they point to until they meet in the middle.
### C Code Implementation
```c
#include <stdio.h>
#include <string.h>
**
* reverseStringInPlace
* 
* Reverses a null-terminated string in place.
* 
* @param str The string (character array) to be reversed.
* @return A pointer to the beginning of the reversed string (the same as the input).
*/
void reverseStringInPlace(char *str) {
if (str == NULL || *str == '\0') {
Handle null pointer or empty string case
return;
}
int start = 0;
int end = strlen(str) - 1;
char temp;
Loop until the start pointer crosses the end pointer
while (start < end) {
1. Store the character at the start position
temp = str[start];


[SYS-3]
**Reversing a String in Place using C**
=====================================================
Here's a simple C function that reverses a string in place. This function uses a two-pointer approach, swapping characters from the start and end of the string until they meet in the middle.
```c
#include <stdio.h>
#include <string.h>
**
* Reverses a string in place.
*
* @param str The string to reverse.
*/
void reverse_string(char* str) {
int left = 0;
int right = strlen(str) - 1;
while (left < right) {
Swap characters at left and right indices
char temp = str[left];
str[left] = str[right];
str[right] = temp;
Move pointers towards the center
left++;
right--;
}
}
```
**Example Usage**
```c
int main() {
char str[] = "Hello, World!";
printf("Original string: %s\n", str);
reverse_string(str);
printf("Reversed string: %s\n", str);
return 0;
}
```
This will output:
```
Original string: Hello, World!
Reversed string:


[SYS-2]
Here is a complete C function to reverse a string in place, along with a `main` function to demonstrate its usage.
### C Code
```c
#include <stdio.h>
#include <string.h>
**
* @brief Reverses a string in place.
* 
* This function swaps characters from the beginning and the end of the 
* string until the middle is reached, effectively reversing the string.
* 
* @param str The string (character array) to be reversed.
*/
void reverseStringInPlace(char *str) {
if (str == NULL) {
return; // Handle null pointer input
}
Get the length of the string
size_t length = strlen(str);
Initialize pointers for the start and end of the string
int start = 0;
int end = (int)length - 1;
Loop until the start pointer crosses or meets the end pointer
while (start < end) {
1. Swap the characters at the start and end positions
char temp = str[start];
