"""R5 — confidence as a pre-registered function of evidence days, recomputed from spans every time.

The design's §3.4. With D_s distinct days carrying supporting spans and D_c distinct days carrying
contradicting spans:

    confidence = 0                              if D_s <= D_c
                 1 - exp(-(D_s - D_c) / tau)    otherwise,  tau = 3 days

Three consistent days give 0.63, five give 0.81, seven give 0.90.

The property that matters is what this function does NOT read: it never takes an earlier confidence
as an input, so a wrong belief cannot feed itself — the self-reinforcing-error mode the research
names. Every call recomputes from the spans alone.

TAU_DAYS and SURFACE_THRESHOLD are pre-registered. They may change only through the benchmark, with
the measured reason written into the design document; they are not tuned to make a run pass.
"""
import math

TAU_DAYS = 3.0
SURFACE_THRESHOLD = 0.80


def confidence(days_support: int, days_contra: int) -> float:
    """The §3.4 function. Contradiction subtracts days, it does not veto: a belief with more
    supporting than contradicting days survives, weaker."""
    net = int(days_support) - int(days_contra)
    if net <= 0:
        return 0.0
    return 1.0 - math.exp(-net / TAU_DAYS)


def distinct_days(iso_timestamps) -> int:
    """How many distinct calendar dates a list of ISO 8601 timestamps covers.

    Sliced rather than parsed: the store writes ISO strings, and the first ten characters are the
    date in every one of them. Parsing would buy nothing and would raise on the naive/aware mix that
    a real transcript pipeline eventually produces.
    """
    return len({str(t)[:10] for t in iso_timestamps if t})


def surfaces(value: float) -> bool:
    """Whether a confidence is high enough to show the owner (the guess is surfaced at >= 0.80)."""
    return value >= SURFACE_THRESHOLD
