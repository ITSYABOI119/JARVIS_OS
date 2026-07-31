/*
 * JARVIS AI-OS - C/M2b Leg A: the MEASURED recall pairs, for the box probe.
 *
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *   generator:  phase3/scripts/embed/gen_recall_pairs.py
 *   source:     phase3/scripts/embed/cm0_recall_set.json :: distinct_positives
 *   drift gate: gen_recall_pairs.py --check
 *   host ref:   phase3/scripts/embed/cm2b_pair_cos.json
 *
 * WHY GENERATED: Leg A compares the box's per-pair cosines against a host
 * reference computed over these exact strings. Retyping them would make any
 * disagreement ambiguous between 'the pipeline differs' and 'the inputs
 * differ', which is the one thing the gate must not be ambiguous about.
 *
 * PROBE-ONLY: self-guarded on JARVIS_EMBED_PROBE, so a deployed build carries
 * none of these strings.
 */

#ifndef EMBED_GATE_PAIRS_H
#define EMBED_GATE_PAIRS_H

#if JARVIS_EMBED_PROBE

#define EMBED_GATE_NPAIRS   36
#define EMBED_GATE_MAXLEN   82   /* longest string, bytes (<< SHMEM_MAX_PAYLOAD 240) */

typedef struct { const char *stored; const char *later; } embed_gate_pair_t;

static const embed_gate_pair_t EMBED_GATE_PAIRS[EMBED_GATE_NPAIRS] = {
    { "what is a page fault",
      "what happens when a program reads memory that isn't currently loaded into RAM" },
    { "how does the tcp three-way handshake work",
      "what are the steps two hosts take to open a tcp connection" },
    { "what is a hash table",
      "which data structure gives average constant-time lookup by key" },
    { "what does DMA do",
      "how can a peripheral move data into memory without the CPU copying it" },
    { "how does DNS work",
      "how is a website name turned into a numeric IP address" },
    { "what is garbage collection",
      "how does a language runtime automatically free memory that is no longer referenced" },
    { "what does a compiler do",
      "what translates human-written source code into machine instructions" },
    { "why do CPUs have caches",
      "why keep a small fast memory between the processor and main RAM" },
    { "what is public-key encryption",
      "how can two parties communicate secretly without sharing a password beforehand" },
    { "what is recursion",
      "what do you call a function that calls itself to solve a smaller subproblem" },
    { "how does binary search work",
      "how do you find a value in a sorted array in logarithmic time" },
    { "what is a mutex",
      "how do you keep two threads from writing the same variable at the same time" },
    { "what is a container like Docker",
      "how do you package an app with its dependencies so it runs anywhere" },
    { "what is a load balancer",
      "how do you spread incoming requests across several backend servers" },
    { "what is a rest api",
      "how do web clients talk to a server over http using resources and verbs" },
    { "what is a file descriptor",
      "what is the small integer the kernel hands you to refer to an open file" },
    { "what is a linked list",
      "which data structure chains nodes together where each points to the next" },
    { "what is a stack data structure",
      "which structure adds and removes items in last-in first-out order" },
    { "what is a queue",
      "which structure processes items in first-in first-out order" },
    { "what is a database index",
      "what speeds up row lookups so the engine doesn't scan the whole table" },
    { "what is a foreign key",
      "how does one table reference the primary key of another table" },
    { "what is http",
      "what protocol do web browsers use to request pages from a server" },
    { "what is json",
      "what lightweight text format stores structured data as key-value pairs" },
    { "what is a regular expression",
      "how do you describe a text-search pattern using special symbols" },
    { "what is big-o notation",
      "how do you describe how an algorithm's running time grows with input size" },
    { "what is a virtual machine",
      "how do you run a full guest operating system on top of a host machine" },
    { "what is a firewall",
      "what filters network traffic to block unwanted or unsafe connections" },
    { "what is a checksum",
      "how do you detect whether data got corrupted during transmission" },
    { "what is a bloom filter",
      "which probabilistic structure tests membership allowing rare false positives" },
    { "what is a heap data structure",
      "which tree keeps the smallest or largest element at the root for a priority queue" },
    { "what is a symbolic link",
      "what special file points to another file by its path name" },
    { "what is network latency",
      "what is the delay before data begins to transfer across a network" },
    { "what is a thread pool",
      "how do you reuse a fixed set of worker threads to run many small tasks" },
    { "what is memoization",
      "how do you cache a function's results to avoid recomputing them" },
    { "what is an environment variable",
      "what named value in the OS configures how a running process behaves" },
    { "what is a uuid",
      "what randomly generated 128-bit identifier is unique without a central authority" },
};

/* The pair invented for the first G2 attempt. NOT part of the measured set and
 * NOT counted in the fire rate - carried so the report can say where it sits
 * relative to the measured distribution. Host cosine: 0.5352 (below the floor). */
#define EMBED_GATE_EXTRA_STORED "what is a page fault"
#define EMBED_GATE_EXTRA_LATER  "explain what happens when a memory page is not resident"

#endif /* JARVIS_EMBED_PROBE */
#endif /* EMBED_GATE_PAIRS_H */
