#!/usr/bin/env python3
"""
JARVIS AI-OS - Stability Test Harness
Phase 2 Week 48

Automated long-running stability test that exercises all seL4 subsystems
via UART IPC: cache queries, shell commands, heartbeat, SHIELD checks,
and network commands.

Logs every test to CSV. Detects hangs, attempts recovery, and prints
a summary report at the end.

Usage:
    # 60-minute stability run on real hardware
    python stability_harness.py --port /dev/ttyUSB0 --duration 60

    # Quick 5-minute test with verbose logging
    python stability_harness.py --port /dev/ttyUSB0 --duration 5 --verbose

    # Self-test (mock mode, no hardware needed)
    python stability_harness.py --self-test
"""

import argparse
import csv
import logging
import os
import random
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Add parent directory to path for import
sys.path.insert(0, str(Path(__file__).resolve().parent))

from uart_ipc_client import UARTIPCClient, MsgType  # noqa: E402

logger = logging.getLogger("stability_harness")

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

CACHE_QUERIES: List[str] = [
    "help", "status", "version", "info", "about", "hello",
    "time", "date", "uptime", "ls", "pwd", "ls -l", "ls -a",
    "cat readme", "ps", "top", "free", "df", "cpu",
    "list files", "show files", "memory", "disk", "network",
]

SHELL_COMMANDS: List[str] = [
    "temp", "watchdog", "gpio", "i2c", "spi", "rng", "pwm",
    "dt", "boot", "power", "usb", "stress",
]

NETWORK_COMMANDS: List[str] = [
    "ifconfig", "netstat",
]

SHIELD_ACTIONS: List[str] = [
    "read_file /etc/passwd",
    "delete_all_logs",
    "reboot_system",
    "install_package curl",
    "modify_firewall",
    "list_users",
    "check_disk",
    "run_diagnostic",
]

HARMFUL_ACTIONS: List[str] = [
    "rm -rf /",
    "format disk",
    "disable_security",
    "exfiltrate_data",
]

HANG_TIMEOUT_SEC: float = 30.0
STATS_PRINT_INTERVAL_SEC: float = 60.0


@dataclass
class TestMix:
    """Configurable test mix ratios (must sum to 1.0)."""
    cache: float = 0.40
    shell: float = 0.25
    heartbeat: float = 0.15
    shield: float = 0.10
    network: float = 0.10


@dataclass
class HarnessConfig:
    """Stability harness configuration."""
    port: str = "/dev/ttyUSB0"
    baud: int = 115200
    duration_minutes: float = 60.0
    interval_sec: float = 1.0
    log_file: str = "stability_log.csv"
    verbose: bool = False
    mock_mode: bool = False
    mix: TestMix = field(default_factory=TestMix)


@dataclass
class HarnessStats:
    """Accumulated test statistics."""
    total: int = 0
    pass_count: int = 0
    fail_count: int = 0
    warn_count: int = 0
    crash_events: int = 0

    cache_hits: int = 0
    cache_misses: int = 0
    cache_total: int = 0

    heartbeat_rtts: List[float] = field(default_factory=list)

    shield_allows: int = 0
    shield_blocks: int = 0
    shield_total: int = 0

    start_time: float = 0.0
    end_time: float = 0.0


# ---------------------------------------------------------------------------
# Stability Harness
# ---------------------------------------------------------------------------

class StabilityHarness:
    """Automated stability test harness for JARVIS seL4 via UART IPC."""

    def __init__(self, config: HarnessConfig):
        self.config = config
        self.stats = HarnessStats()
        self.client: Optional[UARTIPCClient] = None
        self._csv_file = None
        self._csv_writer = None
        self._last_heartbeat_ok: float = 0.0
        self._last_stats_print: float = 0.0
        self._running: bool = False

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    def _connect(self) -> bool:
        """Create and connect the UART IPC client."""
        self.client = UARTIPCClient(
            port=self.config.port,
            baudrate=self.config.baud,
            mock_mode=self.config.mock_mode,
        )
        ok = self.client.connect()
        if ok:
            self._last_heartbeat_ok = time.time()
        return ok

    def _disconnect(self) -> None:
        """Disconnect the UART IPC client."""
        if self.client:
            self.client.disconnect()
            self.client = None

    # ------------------------------------------------------------------
    # CSV logging
    # ------------------------------------------------------------------

    def _open_log(self) -> None:
        """Open the CSV log file and write the header row."""
        self._csv_file = open(
            self.config.log_file, "w", encoding="utf-8", newline=""
        )
        self._csv_writer = csv.writer(self._csv_file)
        self._csv_writer.writerow([
            "timestamp", "test_type", "command",
            "response_summary", "latency_ms", "result",
        ])
        self._csv_file.flush()

    def _close_log(self) -> None:
        """Close the CSV log file."""
        if self._csv_file:
            self._csv_file.close()
            self._csv_file = None
            self._csv_writer = None

    def _log(self, test_type: str, command: str,
             response_summary: str, latency_ms: float,
             result: str) -> None:
        """Write one row to the CSV log."""
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
        if self._csv_writer:
            self._csv_writer.writerow([
                ts, test_type, command,
                response_summary, f"{latency_ms:.1f}", result,
            ])
            self._csv_file.flush()

        # Update stats
        self.stats.total += 1
        if result == "PASS":
            self.stats.pass_count += 1
        elif result == "FAIL":
            self.stats.fail_count += 1
        elif result == "WARN":
            self.stats.warn_count += 1

    # ------------------------------------------------------------------
    # Individual test methods
    # ------------------------------------------------------------------

    def _test_cache_query(self) -> None:
        """Send a random cache lookup and verify the response."""
        query = random.choice(CACHE_QUERIES)
        start = time.time()
        resp = self.client.cache_lookup(query)
        latency_ms = (time.time() - start) * 1000.0

        if resp is None:
            self._log("cache", query, "TIMEOUT", latency_ms, "FAIL")
            return

        hit = resp.get("hit", False)
        action = resp.get("action", "")
        summary = f"{'hit' if hit else 'miss'}:{action}" if action else ("hit" if hit else "miss")
        self._log("cache", query, summary, latency_ms, "PASS")

        self.stats.cache_total += 1
        if hit:
            self.stats.cache_hits += 1
        else:
            self.stats.cache_misses += 1

    def _test_shell_command(self) -> None:
        """Send a random shell command and verify output."""
        cmd = random.choice(SHELL_COMMANDS)
        start = time.time()
        resp = self.client.send_command(cmd)
        latency_ms = (time.time() - start) * 1000.0

        if resp is None:
            self._log("shell", cmd, "TIMEOUT", latency_ms, "FAIL")
            return

        # Truncate long responses for the summary
        summary = resp.replace("\n", " ").strip()
        if len(summary) > 80:
            summary = summary[:77] + "..."
        self._log("shell", cmd, summary, latency_ms, "PASS")

    def _test_heartbeat(self) -> None:
        """Send heartbeat and measure RTT."""
        # Mock mode: send_heartbeat has no mock path, simulate success
        if self.client.in_mock_mode:
            self._last_heartbeat_ok = time.time()
            latency_ms = random.uniform(5.0, 15.0)
            self.stats.heartbeat_rtts.append(latency_ms)
            self._log("heartbeat", "", f"RTT={latency_ms:.1f}ms", latency_ms, "PASS")
            return

        start = time.time()
        ok = self.client.send_heartbeat()
        latency_ms = (time.time() - start) * 1000.0

        if ok:
            self._last_heartbeat_ok = time.time()
            self.stats.heartbeat_rtts.append(latency_ms)
            self._log("heartbeat", "", f"RTT={latency_ms:.1f}ms", latency_ms, "PASS")
        else:
            self._log("heartbeat", "", "TIMEOUT", latency_ms, "FAIL")

    def _test_shield_check(self) -> None:
        """Send a SHIELD risk assessment and verify the response."""
        # Mix in some harmful actions to verify blocking
        if random.random() < 0.3:
            action = random.choice(HARMFUL_ACTIONS)
            expect_block = True
        else:
            action = random.choice(SHIELD_ACTIONS)
            expect_block = False

        start = time.time()
        resp = self.client.shield_check(action)
        latency_ms = (time.time() - start) * 1000.0

        if resp is None:
            self._log("shield", action, "TIMEOUT", latency_ms, "FAIL")
            return

        recommendation = resp.get("recommendation", "unknown")
        risk_score = resp.get("risk_score", -1)
        summary = f"risk={risk_score:.2f},rec={recommendation}"
        blocked = recommendation in ("block", "deny")

        self.stats.shield_total += 1
        if blocked:
            self.stats.shield_blocks += 1
        else:
            self.stats.shield_allows += 1

        # Warn if a harmful action was allowed (but don't fail the whole run)
        if expect_block and not blocked:
            self._log("shield", action, summary, latency_ms, "WARN")
            logger.warning("SHIELD allowed harmful action: %s", action)
        else:
            self._log("shield", action, summary, latency_ms, "PASS")

    def _test_network(self) -> None:
        """Send a network shell command (ifconfig / netstat)."""
        cmd = random.choice(NETWORK_COMMANDS)
        start = time.time()
        resp = self.client.send_command(cmd)
        latency_ms = (time.time() - start) * 1000.0

        if resp is None:
            self._log("network", cmd, "TIMEOUT", latency_ms, "FAIL")
            return

        summary = resp.replace("\n", " ").strip()
        if len(summary) > 80:
            summary = summary[:77] + "..."
        self._log("network", cmd, summary, latency_ms, "PASS")

    # ------------------------------------------------------------------
    # Hang detection and recovery
    # ------------------------------------------------------------------

    def _detect_hang(self) -> bool:
        """
        Check if the system appears hung (no heartbeat ACK in HANG_TIMEOUT_SEC).

        Returns True if a hang was detected and recovery was attempted.
        """
        # Mock mode: heartbeats always succeed, no hangs possible
        if self.client and self.client.in_mock_mode:
            return False

        elapsed = time.time() - self._last_heartbeat_ok
        if elapsed < HANG_TIMEOUT_SEC:
            return False

        logger.warning("Hang detected: %.1fs since last heartbeat ACK", elapsed)
        self.stats.crash_events += 1
        self._log("recovery", "", "hang_detected", elapsed * 1000.0, "WARN")

        # Attempt protocol reset
        logger.info("Attempting protocol reset...")
        self.client.reset_protocol()
        time.sleep(0.5)

        start = time.time()
        ok = self.client.send_heartbeat()
        latency_ms = (time.time() - start) * 1000.0

        if ok:
            self._last_heartbeat_ok = time.time()
            self._log("recovery", "", "protocol_reset_ok", latency_ms, "WARN")
            logger.info("Protocol reset succeeded")
            return True

        # Reset failed -- try full reconnect
        logger.warning("Protocol reset failed, attempting reconnect...")
        self._disconnect()
        time.sleep(1.0)

        if self._connect():
            self._log("recovery", "", "reconnect_ok", 0, "WARN")
            logger.info("Reconnect succeeded")
        else:
            self._log("recovery", "", "reconnect_failed", 0, "FAIL")
            logger.error("Reconnect failed -- continuing anyway")

        return True

    # ------------------------------------------------------------------
    # Test selection
    # ------------------------------------------------------------------

    def _pick_test(self) -> str:
        """Randomly pick a test type based on configured mix ratios."""
        r = random.random()
        cumulative = 0.0
        mix = self.config.mix
        for name, weight in [
            ("cache", mix.cache),
            ("shell", mix.shell),
            ("heartbeat", mix.heartbeat),
            ("shield", mix.shield),
            ("network", mix.network),
        ]:
            cumulative += weight
            if r < cumulative:
                return name
        return "cache"  # fallback

    def _run_test(self, test_type: str) -> None:
        """Dispatch to the appropriate test method."""
        dispatch = {
            "cache": self._test_cache_query,
            "shell": self._test_shell_command,
            "heartbeat": self._test_heartbeat,
            "shield": self._test_shield_check,
            "network": self._test_network,
        }
        fn = dispatch.get(test_type, self._test_cache_query)
        try:
            fn()
        except Exception as exc:
            logger.error("Test %s raised exception: %s", test_type, exc)
            self._log(test_type, "", f"exception:{exc}", 0, "FAIL")

    # ------------------------------------------------------------------
    # Periodic status
    # ------------------------------------------------------------------

    def _maybe_print_stats(self) -> None:
        """Print running stats to stdout every STATS_PRINT_INTERVAL_SEC."""
        now = time.time()
        if now - self._last_stats_print < STATS_PRINT_INTERVAL_SEC:
            return
        self._last_stats_print = now

        elapsed = now - self.stats.start_time
        rate = self.stats.total / elapsed if elapsed > 0 else 0
        pct = (self.stats.pass_count / self.stats.total * 100.0
               if self.stats.total else 0)
        print(
            f"[{elapsed / 60:.1f} min] "
            f"tests={self.stats.total} "
            f"pass={self.stats.pass_count} ({pct:.1f}%) "
            f"fail={self.stats.fail_count} "
            f"warn={self.stats.warn_count} "
            f"crashes={self.stats.crash_events} "
            f"rate={rate:.1f}/s"
        )

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------

    def run(self) -> None:
        """Main test loop -- runs for the configured duration."""
        print(f"=== JARVIS Stability Harness ===")
        print(f"Port:     {self.config.port}")
        print(f"Duration: {self.config.duration_minutes} min")
        print(f"Interval: {self.config.interval_sec}s")
        print(f"Log:      {self.config.log_file}")
        print(f"Mock:     {self.config.mock_mode}")
        print()

        # Connect
        if not self._connect():
            print("ERROR: Failed to connect to UART. Aborting.")
            return

        # Open CSV log
        self._open_log()

        self.stats.start_time = time.time()
        self._last_stats_print = self.stats.start_time
        self._running = True
        deadline = self.stats.start_time + (self.config.duration_minutes * 60.0)

        try:
            while self._running and time.time() < deadline:
                # Hang detection (before each test)
                self._detect_hang()

                # Pick and run a test
                test_type = self._pick_test()
                self._run_test(test_type)

                # Periodic status print
                self._maybe_print_stats()

                # Inter-test delay
                time.sleep(self.config.interval_sec)

        except KeyboardInterrupt:
            print("\nInterrupted by user.")
        finally:
            self.stats.end_time = time.time()
            self._running = False
            self._close_log()
            self._disconnect()

        self.print_summary()

    # ------------------------------------------------------------------
    # Summary report
    # ------------------------------------------------------------------

    def print_summary(self) -> None:
        """Print the final test summary report."""
        s = self.stats
        actual_min = (s.end_time - s.start_time) / 60.0 if s.end_time > s.start_time else 0

        pass_pct = (s.pass_count / s.total * 100.0) if s.total else 0
        fail_pct = (s.fail_count / s.total * 100.0) if s.total else 0
        warn_pct = (s.warn_count / s.total * 100.0) if s.total else 0

        cache_hit_rate = (
            s.cache_hits / s.cache_total * 100.0 if s.cache_total else 0
        )

        rtts = s.heartbeat_rtts
        if rtts:
            avg_rtt = sum(rtts) / len(rtts)
            min_rtt = min(rtts)
            max_rtt = max(rtts)
        else:
            avg_rtt = min_rtt = max_rtt = 0.0

        shield_block_rate = (
            s.shield_blocks / s.shield_total * 100.0 if s.shield_total else 0
        )

        print()
        print("=== JARVIS Stability Test Report ===")
        print(f"Duration: {self.config.duration_minutes} min (requested), "
              f"{actual_min:.1f} min (actual)")
        print(f"Total tests: {s.total:,}")
        print(f"  PASS: {s.pass_count:,} ({pass_pct:.1f}%)")
        print(f"  FAIL: {s.fail_count:,} ({fail_pct:.1f}%)")
        print(f"  WARN: {s.warn_count:,} ({warn_pct:.1f}%)")
        print(f"Crash events: {s.crash_events}")
        if rtts:
            print(f"Heartbeat RTT: avg={avg_rtt:.1f}ms, "
                  f"min={min_rtt:.1f}ms, max={max_rtt:.1f}ms")
        else:
            print("Heartbeat RTT: n/a")
        print(f"Cache hit rate: {cache_hit_rate:.1f}% "
              f"({s.cache_hits}/{s.cache_total})")
        print(f"SHIELD block rate: {shield_block_rate:.1f}% "
              f"({s.shield_blocks}/{s.shield_total})")
        print(f"Log file: {self.config.log_file} ({s.total:,} entries)")
        print("====================================")


# ---------------------------------------------------------------------------
# Self-tests
# ---------------------------------------------------------------------------

def _self_test_connection() -> bool:
    """Test: create UARTIPCClient in mock mode, connect/disconnect."""
    print("  test_connection ... ", end="", flush=True)
    client = UARTIPCClient(mock_mode=True)
    ok = client.connect()
    if not ok:
        print("FAIL (connect returned False)")
        return False
    if not client.connected:
        print("FAIL (connected flag not set)")
        return False
    client.disconnect()
    if client.connected:
        print("FAIL (still connected after disconnect)")
        return False
    print("PASS")
    return True


def _self_test_command_parse() -> bool:
    """Test: send 'temp' command in mock mode, verify response."""
    print("  test_command_parse ... ", end="", flush=True)
    client = UARTIPCClient(mock_mode=True)
    client.connect()
    resp = client.send_command("temp")
    client.disconnect()
    if resp is None:
        print("FAIL (response is None)")
        return False
    if "temp" not in resp.lower() and "mock" not in resp.lower():
        print(f"FAIL (unexpected response: {resp!r})")
        return False
    print("PASS")
    return True


def _self_test_log_creation() -> bool:
    """Test: create CSV log, write entries, verify format."""
    print("  test_log_creation ... ", end="", flush=True)
    log_path = "_self_test_stability.csv"
    try:
        config = HarnessConfig(
            mock_mode=True,
            duration_minutes=0,
            log_file=log_path,
        )
        harness = StabilityHarness(config)
        harness._open_log()
        harness._log("cache", "help", "hit:exec_help", 2.3, "PASS")
        harness._log("heartbeat", "", "RTT=7.2ms", 7.2, "PASS")
        harness._log("shell", "temp", "Temperature: 45C", 8.1, "PASS")
        harness._close_log()

        # Verify the file
        if not os.path.exists(log_path):
            print("FAIL (file not created)")
            return False

        with open(log_path, "r", encoding="utf-8") as f:
            reader = csv.reader(f)
            rows = list(reader)

        if len(rows) != 4:  # 1 header + 3 data
            print(f"FAIL (expected 4 rows, got {len(rows)})")
            return False

        header = rows[0]
        if header[0] != "timestamp" or header[1] != "test_type":
            print(f"FAIL (bad header: {header})")
            return False

        if rows[1][1] != "cache" or rows[1][5] != "PASS":
            print(f"FAIL (bad data row: {rows[1]})")
            return False

        if harness.stats.total != 3 or harness.stats.pass_count != 3:
            print(f"FAIL (stats wrong: total={harness.stats.total})")
            return False

        print("PASS")
        return True
    finally:
        if os.path.exists(log_path):
            os.remove(log_path)


def _self_test_harness_mock_run() -> bool:
    """Test: run harness in mock mode for a few seconds."""
    print("  test_harness_mock_run ... ", end="", flush=True)
    log_path = "_self_test_mock_run.csv"
    try:
        config = HarnessConfig(
            mock_mode=True,
            duration_minutes=0.05,  # 3 seconds
            interval_sec=0.1,
            log_file=log_path,
        )
        harness = StabilityHarness(config)
        harness.run()

        if harness.stats.total < 5:
            print(f"FAIL (only {harness.stats.total} tests in 3s)")
            return False

        if harness.stats.fail_count > 0:
            print(f"FAIL ({harness.stats.fail_count} failures in mock mode)")
            return False

        if not os.path.exists(log_path):
            print("FAIL (log file missing)")
            return False

        print(f"PASS ({harness.stats.total} tests)")
        return True
    finally:
        if os.path.exists(log_path):
            os.remove(log_path)


def run_self_tests() -> bool:
    """Run all self-tests. Returns True if all pass."""
    print("=== Stability Harness Self-Tests ===")
    results = [
        _self_test_connection(),
        _self_test_command_parse(),
        _self_test_log_creation(),
        _self_test_harness_mock_run(),
    ]
    passed = sum(results)
    total = len(results)
    print(f"\n{passed}/{total} self-tests passed")
    return all(results)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="JARVIS AI-OS Stability Test Harness"
    )
    parser.add_argument(
        "--port", default="/dev/ttyUSB0",
        help="Serial port (default: /dev/ttyUSB0)",
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "--duration", type=float, default=60.0,
        metavar="MINUTES",
        help="Test duration in minutes (default: 60)",
    )
    parser.add_argument(
        "--interval", type=float, default=1.0,
        metavar="SECONDS",
        help="Seconds between tests (default: 1.0)",
    )
    parser.add_argument(
        "--log", default="stability_log.csv",
        metavar="LOG_FILE",
        help="CSV log file path (default: stability_log.csv)",
    )
    parser.add_argument(
        "--self-test", action="store_true",
        help="Run self-tests in mock mode and exit",
    )
    parser.add_argument(
        "--mock", action="store_true",
        help="Force mock mode (no real hardware)",
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Enable verbose logging to stderr",
    )

    args = parser.parse_args()

    # Configure logging
    level = logging.DEBUG if args.verbose else logging.WARNING
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        stream=sys.stderr,
    )

    if args.self_test:
        ok = run_self_tests()
        sys.exit(0 if ok else 1)

    config = HarnessConfig(
        port=args.port,
        baud=args.baud,
        duration_minutes=args.duration,
        interval_sec=args.interval,
        log_file=args.log,
        verbose=args.verbose,
        mock_mode=args.mock,
    )

    harness = StabilityHarness(config)
    harness.run()


if __name__ == "__main__":
    main()
