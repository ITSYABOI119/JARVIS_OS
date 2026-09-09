"""The MS1b extractor: spans in, registry-typed candidates out.

The design's §4.2. One LLM call per span, schema-constrained so the model SELECTS a predicate id, a
relation id, a polarity and a source kind from enums GENERATED from the registry — never a free
string. That is the K-b boundary applied to extraction: a known vocabulary selects the candidate,
the model never invents one, and an id the registry does not carry cannot be emitted at all.

Nothing here reads the store. The extractor sees a span's verbatim text, its speaker cluster, the
day and the household's known names, and nothing else — so an extraction can never be contaminated
by what the store already believes.

Stdlib only: `json`, `urllib`, `subprocess`. The model runs in a llama.cpp server this package
starts and stops; no Python ML stack is imported, and CI never loads a model.
"""
from .schema import candidate_schema, schema_sha256          # noqa: F401
from .prompt import system_prompt, user_prompt               # noqa: F401
from .score import (                                          # noqa: F401
    lenient_match, match, resolve_subject, score_household, validity,
)
