# BLIND QUALITY JUDGING SET — JARVIS control-IN workload

Four anonymised systems answered the same 12 questions. Identities are withheld.

Each system has a FIXED label ([SYS-1]..[SYS-4]) meaning the SAME system on every
question, so per-system scoring is valid. Only the ORDER they appear in is shuffled
per question — POSITION carries no information, the label does.


========================================================================
## Q1. explain paging in one line
========================================================================


[SYS-2]
Paging is a memory management scheme that divides physical memory into fixed-size blocks called frames and logical memory into similarly sized blocks called pages.


[SYS-1]
**Paging is a memory management technique that divides both physical memory and logical memory into fixed-size blocks to allow efficient allocation and protection.**


[SYS-3]
Paging is a memory management technique where a large memory space is divided into smaller, fixed-size blocks called pages, allowing for efficient allocation and deallocation of memory.


[SYS-4]
Paging is a memory management technique where the operating system divides the physical memory into fixed-size blocks called pages to manage memory efficiently.


========================================================================
## Q2. in one line, what is a mutex
========================================================================


[SYS-4]
A mutex (short for mutual exclusion) is a synchronization mechanism that allows only one thread to access a shared resource at a time, preventing data corruption and ensuring thread safety.


[SYS-2]
A mutex (mutual exclusion) is a synchronization primitive used to protect shared resources from being accessed by multiple threads simultaneously.


[SYS-3]
A mutex (short for "mutual exclusion") is a synchronization mechanism that allows only one thread to access a shared resource at a time, preventing data corruption and ensuring thread safety.


[SYS-1]
A mutex (mutual exclusion) is a synchronization primitive used to protect shared resources from being accessed by multiple threads simultaneously.


========================================================================
## Q3. in one line, what is a translation lookaside buffer
========================================================================


[SYS-3]
A Translation Lookaside Buffer (TLB) is a cache memory that stores recent translations of virtual addresses to physical addresses, reducing the time it takes for a CPU to access memory.


[SYS-1]
A Translation Lookaside Buffer (TLB) is a small, fast cache used by the CPU to speed up virtual-to-physical address translation by storing recent mappings.


[SYS-2]
A translation lookaside buffer (TLB) is a small, fast cache used by the Memory Management Unit (MMU) to speed up the process of translating virtual memory addresses to physical memory addresses.


[SYS-4]
A translation lookaside buffer (TLB) is a cache memory that stores recent translations of virtual addresses to physical addresses, improving the efficiency of memory access in computer systems.


========================================================================
## Q4. in one line, what is a memory management unit
========================================================================


[SYS-2]
A memory management unit is a hardware component responsible for managing the computer's main memory.


[SYS-1]
A Memory Management Unit (MMU) is a hardware component that handles the translation of virtual memory addresses to physical memory addresses.


[SYS-3]
A Memory Management Unit (MMU) is a hardware component that translates virtual memory addresses to physical memory addresses, enabling efficient and secure memory management in computer systems.


[SYS-4]
A memory management unit (MMU) is a hardware component that translates virtual memory addresses to physical memory addresses, enabling efficient and secure memory management in computer systems.


========================================================================
## Q5. in one line, what does the seL4 capability system provide
========================================================================


[SYS-4]
The seL4 capability system provides a formal, provably correct, and highly secure mechanism for enforcing access control and memory protection in operating systems.


[SYS-3]
The seL4 capability system provides a formal, provably correct, and highly secure mechanism for enforcing access control and memory protection in operating systems.


[SYS-2]
The seL4 capability system provides a formally verified, fine-grained access control mechanism for operating system kernels and applications.


[SYS-1]
The seL4 capability system provides a **formal, mathematically verified foundation for secure and fine-grained isolation and access control** in operating systems and critical systems.


========================================================================
## Q6. what is a page fault?
========================================================================


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


[SYS-4]
A page fault is an event that occurs when a program attempts to access a memory page that is not currently in the computer's memory (RAM), requiring the operating system to retrieve the page from the hard drive or swap space.


[SYS-2]
A page fault is an interrupt that occurs when a program tries to access a page of memory that is not currently loaded into physical memory.


[SYS-3]
A page fault is an event that occurs when a program attempts to access a memory page that is not currently in the physical memory (RAM) of a computer. When a program requests access to a memory page, the operating system checks if the page is in the physical memory. If it is not, the operating system generates a page fault exception.
Here's what happens when a page fault occurs:
1. **Page Fault Exception**: The operating system generates a page fault exception, which is a signal to the CPU that a page fault has occurred.
2. **Page Table Walk**: The operating system performs a page table walk to determine the location of the requested page in the virtual memory.
3. **Page Replacement**: The operating system selects a page to replace in the physical memory, using a page replacement algorithm (e.g., LRU, FIFO, or Optimal).
4. **Page In**: The operating system loads the requested page from the virtual memory into the physical memory.
5. **Page Fault Handling**: The operating system handles the page fault by updating the page table, updating the process's memory map, and potentially updating the disk cache.
Page faults can be categorized into two types:
1. **Minor Page Fault**: A minor page fault


========================================================================
## Q7. why doesn't adding more CPU cores speed up a single-threaded program?
========================================================================


[SYS-1]
This is a fundamental concept in computer architecture and parallel programming. The reason adding more CPU cores **doesn't speed up a single-threaded program** is because of **how a single-threaded program executes** and the **nature of parallelism**.
Here is a detailed breakdown of why this is the case:
## 1. The Bottleneck: The Single Thread
A single-threaded program executes its instructions sequentially, one after the other, on a single CPU core.
* **Sequential Execution:** Imagine a chef (the CPU core) making a complex meal (the program). They must complete Step 1 entirely before starting Step 2, and so on.
* **The Limiting Factor:** The speed of the program is limited by the time it takes for that single core to execute all the instructions. If the program is CPU-bound (meaning it spends most of its time performing calculations), the speed is directly proportional to the clock speed and efficiency of that single core.
**Analogy:** If you have one worker trying to build a wall, adding more workers won't make the *single* worker build the wall faster; it just means you have more people available to build *multiple* walls simultaneously.


[SYS-2]
Adding more CPU cores does not speed up a single-threaded program because a single-threaded program only utilizes one CPU core at a time, and therefore cannot benefit from parallel execution.


[SYS-3]
Adding more CPU cores can speed up a program that is multi-threaded or multi-process, but it won't have a significant impact on a single-threaded program. Here's why:
1. **Single-threaded programs only use one core**: Since the program is single-threaded, it can only execute one instruction at a time. This means that even if you have multiple CPU cores, the program will only use one core at a time.
2. **No parallelism**: Single-threaded programs don't have the ability to execute multiple instructions simultaneously. They execute one instruction after another, in a linear fashion.
3. **No thread-level parallelism**: Even if the program is using a multi-core CPU, the operating system will still schedule the single thread on one core at a time. This means that the additional cores are not being utilized.
4. **No data-level parallelism**: Single-threaded programs typically don't have the ability to execute multiple instructions on different data simultaneously. They execute one instruction on one piece of data at a time.
However, there are some scenarios where adding more CPU cores can still have a positive impact on single-threaded programs:
1. **I/O-bound operations**: If the program is waiting for I/O


[SYS-4]
Adding more CPU cores does not speed up a single-threaded program because the program can only utilize one core at a time, and the additional cores remain idle until the program is modified to take advantage of multi-threading or parallel processing.


========================================================================
## Q8. In one sentence, why doesn't adding more CPU cores speed up a single-threaded program?
========================================================================


[SYS-2]
Adding more CPU cores does not speed up a single-threaded program because a single-threaded program only utilizes one CPU core at a time, and therefore cannot benefit from parallel processing.


[SYS-3]
Adding more CPU cores does not speed up a single-threaded program because the program can only utilize one core at a time, and the additional cores remain idle until the program is modified to take advantage of multi-threading or parallel processing.


[SYS-1]
Adding more CPU cores does not speed up a single-threaded program because that program is inherently limited by the speed of a single processor core.


[SYS-4]
Adding more CPU cores does not speed up a single-threaded program because the program can only utilize one core at a time, and the additional cores remain idle until the program is modified to take advantage of multi-threading.


========================================================================
## Q9. Explain how virtual memory works in three sentences.
========================================================================


[SYS-3]
Virtual memory is a combination of physical RAM (Random Access Memory) and a storage device, such as a hard drive, that acts as an extension of the RAM to provide more memory space for running applications. When the system runs low on physical RAM, the operating system moves less frequently used data from RAM to the storage device, freeing up space in RAM for more critical data. This process, known as paging or swapping, allows the system to use more memory than is physically available, making it appear as if the system has more RAM than it actually does.


[SYS-4]
Virtual memory is a system that allows a computer to use a combination of physical RAM and hard drive space to increase the amount of memory available for running applications.
When a program requests memory, the operating system checks if there is enough physical RAM available. If there is not enough RAM, the operating system moves some of the least recently used data from RAM to the hard drive, freeing up space in RAM for the new program. This data is then stored on the hard drive as a file, and the operating system can swap it back into RAM when needed, allowing the program to continue running.


[SYS-1]
Virtual memory allows a computer to run programs larger than the physical RAM by using disk space as an extension of memory. It creates an illusion for each process, giving it a contiguous, large address space regardless of physical memory availability. This system efficiently manages memory by swapping inactive data between RAM and the hard drive as needed.


[SYS-2]
Virtual memory is a memory management technique that allows a computer to compensate for a lack of physical memory by temporarily transferring data from RAM to disk storage. It creates the illusion of a much larger memory space than is physically available to the system. This process enables programs to run even if they are larger than the physical memory, by only loading the parts of the program that are actively being used into RAM.


========================================================================
## Q10. What is the difference between a process and a thread?
========================================================================


[SYS-2]
The main difference between a process and a thread lies in their scope of resource ownership and execution. A process is an independent program in execution with its own dedicated memory space and resources, whereas a thread is a path of execution within a process, sharing the process's memory space and resources.


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


[SYS-3]
**Process vs Thread: Key Differences**
In computing, both processes and threads are used to execute programs, but they have distinct differences in terms of their execution, memory management, and resource utilization.
### Process
A **process** is an independent program that runs in its own memory space. Each process has its own:
*   **Memory space**: Each process has its own virtual address space, which means they can't access each other's memory directly.
*   **Resources**: Each process has its own set of system resources, such as open files, network connections, and system calls.
*   **Execution**: Each process executes independently, with its own program counter, stack, and registers.
### Thread
A **thread**, on the other hand, is a lightweight process that shares the same memory space as its parent process. Threads have:
*   **Shared memory space**: Threads share the same virtual address space as their parent process, which means they can access each other's memory directly.
*   **Shared resources**: Threads share the same set of system resources as their parent process, such as open files and network connections.
*   **Execution**: Threads execute concurrently within the same process, with their own program counter, stack, and registers.


[SYS-4]
A process and a thread are both used to execute tasks in a computer system, but the main difference lies in the level of resource sharing and the degree of independence between them.


========================================================================
## Q11. Describe the TCP three-way handshake step by step.
========================================================================


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


[SYS-2]
The TCP three-way handshake is a process used to establish a reliable connection between a client and a server.
Here are the steps:
1. **SYN (Synchronize):** The client initiates the connection by sending a TCP segment with the SYN flag set to the server. This message contains an initial sequence number (ISN) chosen by the client.
2. **SYN-ACK (Synchronize-Acknowledge):** The server receives the SYN request, acknowledges it by sending a segment with both the SYN and ACK flags set. This segment contains the server's own initial sequence number and an acknowledgment number equal to the client's sequence number plus one.
3. **ACK (Acknowledge):** The client receives the SYN-ACK, acknowledges it by sending a final TCP segment with the ACK flag set. This segment contains an acknowledgment number equal to the server's sequence number plus one.
Once the third step is completed, the connection is established, and data transfer can begin.


[SYS-3]
The TCP three-way handshake, also known as the TCP handshake or the 3-way handshake, is a process used by TCP (Transmission Control Protocol) to establish a connection between two devices, typically a client and a server. Here's a step-by-step explanation of the process:
**Step 1: SYN (Synchronize) - Client**
1. The client initiates the connection by sending a SYN packet to the server. This packet contains the client's initial sequence number (ISN) and other connection parameters.
2. The client sets the SYN flag in the packet header to 1, indicating that this is the first packet in the handshake process.
**Step 2: SYN-ACK (Synchronize-Acknowledgment) - Server**
1. The server receives the SYN packet from the client and responds with a SYN-ACK packet.
2. The server sets the SYN flag to 1 and the ACK flag to 1 in the packet header, indicating that it has received the client's SYN packet and is acknowledging it.
3. The server also sets its own initial sequence number (ISN) in the packet header.
**Step 3: ACK (Acknowledgment) - Client**
1. The client receives the SYN-


[SYS-4]
The TCP three-way handshake is a process used to establish a connection between a client and a server over a network. 
Here's a step-by-step description of the TCP three-way handshake:
1. **SYN (Synchronize) Packet from Client**: The client initiates the connection by sending a SYN packet to the server. This packet includes the client's initial sequence number (ISN) and other connection parameters.
2. **SYN-ACK (Synchronize-Acknowledgment) Packet from Server**: The server receives the SYN packet and responds with a SYN-ACK packet. This packet includes the server's ISN and an acknowledgment of the client's ISN.
3. **ACK (Acknowledgment) Packet from Client**: The client receives the SYN-ACK packet and sends an ACK packet to the server. This packet acknowledges the server's ISN and completes the three-way handshake.
After the three-way handshake is complete, the client and server can exchange data over the established connection.


========================================================================
## Q12. Write a C function that reverses a string in place.
========================================================================


[SYS-4]
```c
#include <stdio.h>
#include <string.h>
void reverse_string(char* str) {
    int left = 0;
    int right = strlen(str) - 1;
    while (left < right) {
        // Swap characters at left and right indices
        char temp = str[left];
        str[left] = str[right];
        str[right] = temp;
        // Move towards the center
        left++;
        right--;
    }
}
```
This function uses a two-pointer approach to swap characters from the start and end of the string, moving towards the center. It does not allocate any additional memory, making it an in-place operation.


[SYS-2]
Here is a C function that reverses a string in place.
```c
#include <stdio.h>
#include <string.h>
/**
 * Reverses a string in place.
 *
 * @param str The string to be reversed.
 * @return A pointer to the beginning of the reversed string (the original string).
 */
char* reverseStringInPlace(char* str) {
    if (str == NULL) {
        return NULL;
    }
    int length = strlen(str);
    int start = 0;
    int end = length - 1;
    char temp;
    while (start < end) {
        // Swap characters
        temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        // Move pointers inward
        start++;
        end--;
    }
    return str;
}
int main() {
    // Test case 1: Normal string
    char str1[] = "hello";
    printf("Original string 1: %s\n", str1);
    reverseStringIn


[SYS-3]
**Reversing a String in Place using C**
Here's a simple C function that reverses a string in place. This function uses a two-pointer approach, swapping characters from the start and end of the string until they meet in the middle.
```c
#include <stdio.h>
#include <string.h>
/**
 * Reverses a string in place.
 *
 * @param str The string to reverse.
 */
void reverse_string(char* str) {
    int left = 0;
    int right = strlen(str) - 1;
    while (left < right) {
        // Swap characters at left and right indices
        char temp = str[left];
        str[left] = str[right];
        str[right] = temp;
        // Move pointers towards the center
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
**Output**
```
Original string: Hello, World!
Reversed string:


[SYS-1]
Here is a C function that reverses a string in place.
This implementation uses two pointers, one starting at the beginning and one at the end of the string, and swaps the characters they point to until they meet in the middle.
### C Implementation
```c
#include <stdio.h>
#include <string.h>
/**
 * reverseStringInPlace
 * 
 * Reverses the characters of a null-terminated string in place.
 * 
 * @param str The string (character array) to be reversed.
 */
void reverseStringInPlace(char *str) {
    if (str == NULL) {
        // Handle the case where a NULL pointer is passed
        return;
    }
    // Get the length of the string
    int length = strlen(str);
    // Initialize two pointers: one at the start and one at the end
    int start = 0;
    int end = length - 1;
    
    // Temporary character to hold the swapped value
    char temp;
    // Loop until the pointers meet or cross each other
    while (start < end) {
        //
