"""The MS0 benchmark — our own household corpus, with the bands pre-registered before the code.

`corpus.generate_household(seed)` plants a known household as a template transcript plus the ORACLE
candidates an extractor would have produced from it. `harness.run(...)` replays it day by day into a
real MemoryStore and scores the five sets of the design's §8.

Everything here is synthetic and seeded: no recording, no transcript and nothing about the owner or
anyone in his household is read or written. The corpus is a template, not a language model's output
- which is exactly why MS0's numbers measure the STORE and not an extractor. MS1 replaces the oracle
candidates with a real extractor's and re-measures against the same sets.
"""
