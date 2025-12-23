#!/usr/bin/env python3
"""
Week 22: Suspend/Resume Stability Testing

Runs continuous suspend/resume cycles to test stability over time.
Tracks success rates, failures, and performance degradation.

Usage:
    python test_suspend_stability.py [duration_minutes]

Default: 60 minutes (1 hour)
For quick test: python test_suspend_stability.py 5

Author: JARVIS AI-OS Team
Date: December 2025
"""

import sys
import time
import random
import logging
import tempfile
import shutil
from pathlib import Path
from datetime import datetime, timedelta
from typing import List, Tuple

# Add AI directory to path
sys.path.insert(0, str(Path(__file__).parent))

from suspend_manager import SuspendManager
from device_agent import DeviceAgent
from network_agent import NetworkAgent
from filesystem_agent import FileSystemAgent
from user_agent import UserAgent
from system_state_manager import SystemStateManager

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)


class SuspendStabilityTest:
    """
    Stability test for suspend/resume functionality.

    Runs continuous suspend/resume cycles with:
    - Random suspend durations (10s - 5min)
    - Random run durations (1-10min)
    - Success/failure tracking
    - Performance monitoring
    """

    def __init__(self, duration_minutes: int = 60):
        """
        Initialize stability test.

        Args:
            duration_minutes: Test duration in minutes (default: 60)
        """
        self.duration = timedelta(minutes=duration_minutes)
        self.temp_dir = tempfile.mkdtemp()
        self.suspend_mgr = SuspendManager(state_dir=self.temp_dir)

        # Create and register components
        self.device_agent = DeviceAgent()
        self.network_agent = NetworkAgent()
        self.fs_agent = FileSystemAgent()
        self.user_agent = UserAgent()
        self.state_mgr = SystemStateManager()

        self.suspend_mgr.register_component("device_agent", self.device_agent)
        self.suspend_mgr.register_component("network_agent", self.network_agent)
        self.suspend_mgr.register_component("fs_agent", self.fs_agent)
        self.suspend_mgr.register_component("user_agent", self.user_agent)
        self.suspend_mgr.register_component("system_state", self.state_mgr)

        # Metrics
        self.total_cycles = 0
        self.successful_suspends = 0
        self.successful_resumes = 0
        self.failures: List[Tuple[str, datetime, str]] = []
        self.suspend_times: List[float] = []
        self.resume_times: List[float] = []

        logger.info(f"Stability test initialized (duration: {duration_minutes} minutes)")
        logger.info(f"State directory: {self.temp_dir}")

    def cleanup(self):
        """Clean up temporary directory"""
        try:
            shutil.rmtree(self.temp_dir, ignore_errors=True)
            logger.info("Cleaned up temporary directory")
        except Exception as e:
            logger.error(f"Cleanup failed: {e}")

    def run(self):
        """Run stability test"""
        start_time = datetime.now()
        end_time = start_time + self.duration

        logger.info("=" * 70)
        logger.info(f"STABILITY TEST START: {start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        logger.info(f"Target end time: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")
        logger.info("=" * 70)
        logger.info("")

        try:
            while datetime.now() < end_time:
                self.total_cycles += 1
                cycle_start = datetime.now()

                logger.info(f"[Cycle {self.total_cycles}] Starting...")

                try:
                    # Random suspend duration (10s - 5min)
                    suspend_duration = random.uniform(10, 300)

                    # Modify agent state (simulate activity)
                    self.device_agent.total_queries += random.randint(1, 10)
                    self.network_agent.total_queries += random.randint(1, 10)

                    # Suspend
                    suspend_start = time.time()
                    if self.suspend_mgr.suspend():
                        suspend_time = time.time() - suspend_start
                        self.suspend_times.append(suspend_time)
                        self.successful_suspends += 1
                        logger.info(f"[Cycle {self.total_cycles}] Suspended ({suspend_time:.2f}s)")
                    else:
                        logger.error(f"[Cycle {self.total_cycles}] Suspend FAILED")
                        self.failures.append(("suspend", datetime.now(), "Suspend failed"))
                        continue

                    # Simulate sleep (wait for suspend duration)
                    logger.info(f"[Cycle {self.total_cycles}] Sleeping for {suspend_duration:.1f}s...")
                    time.sleep(suspend_duration)

                    # Resume
                    resume_start = time.time()
                    if self.suspend_mgr.resume():
                        resume_time = time.time() - resume_start
                        self.resume_times.append(resume_time)
                        self.successful_resumes += 1
                        logger.info(f"[Cycle {self.total_cycles}] Resumed ({resume_time:.2f}s)")
                    else:
                        logger.error(f"[Cycle {self.total_cycles}] Resume FAILED")
                        self.failures.append(("resume", datetime.now(), "Resume failed"))
                        continue

                    # Verify agent state preserved
                    if self.device_agent.total_queries == 0:
                        logger.warning(f"[Cycle {self.total_cycles}] Agent state NOT preserved!")
                        self.failures.append(("state", datetime.now(), "State not preserved"))

                    # Run for a bit before next cycle (1-10 min)
                    run_duration = random.uniform(60, 600)
                    remaining = (end_time - datetime.now()).total_seconds()

                    # Don't sleep longer than remaining time
                    if run_duration > remaining:
                        run_duration = max(0, remaining - 60)  # Leave 60s buffer

                    if run_duration > 0:
                        logger.info(f"[Cycle {self.total_cycles}] Running for {run_duration:.1f}s...")
                        time.sleep(run_duration)

                    cycle_time = (datetime.now() - cycle_start).total_seconds()
                    logger.info(f"[Cycle {self.total_cycles}] Complete ({cycle_time:.1f}s)")
                    logger.info("")

                except Exception as e:
                    logger.error(f"[Cycle {self.total_cycles}] Exception: {e}")
                    self.failures.append(("exception", datetime.now(), str(e)))

        except KeyboardInterrupt:
            logger.info("\n" + "=" * 70)
            logger.info("TEST INTERRUPTED BY USER")
            logger.info("=" * 70)

        finally:
            # Always print report
            self.print_report()
            self.cleanup()

    def print_report(self):
        """Print final stability report"""
        end_time = datetime.now()

        print("\n" + "=" * 70)
        print("SUSPEND/RESUME STABILITY TEST RESULTS")
        print("=" * 70)
        print()

        # Summary
        print("Summary:")
        print(f"  Total cycles: {self.total_cycles}")
        print(f"  Successful suspends: {self.successful_suspends}")
        print(f"  Successful resumes: {self.successful_resumes}")
        print(f"  Failures: {len(self.failures)}")
        print()

        # Success rates
        if self.total_cycles > 0:
            suspend_rate = (self.successful_suspends / self.total_cycles) * 100
            resume_rate = (self.successful_resumes / self.total_cycles) * 100
            print(f"  Suspend success rate: {suspend_rate:.1f}% (target: >95%)")
            print(f"  Resume success rate: {resume_rate:.1f}% (target: >95%)")
            print()

        # Timing statistics
        if self.suspend_times:
            avg_suspend = sum(self.suspend_times) / len(self.suspend_times)
            max_suspend = max(self.suspend_times)
            min_suspend = min(self.suspend_times)

            print("Suspend Timing:")
            print(f"  Average: {avg_suspend:.2f}s (target: <5s)")
            print(f"  Min: {min_suspend:.2f}s")
            print(f"  Max: {max_suspend:.2f}s")
            print()

        if self.resume_times:
            avg_resume = sum(self.resume_times) / len(self.resume_times)
            max_resume = max(self.resume_times)
            min_resume = min(self.resume_times)

            print("Resume Timing:")
            print(f"  Average: {avg_resume:.2f}s (target: <15s)")
            print(f"  Min: {min_resume:.2f}s")
            print(f"  Max: {max_resume:.2f}s")
            print()

        # Failures
        if self.failures:
            print("Failures:")
            for failure_type, failure_time, message in self.failures[:10]:  # Show first 10
                print(f"  [{failure_time.strftime('%H:%M:%S')}] {failure_type}: {message}")
            if len(self.failures) > 10:
                print(f"  ... and {len(self.failures) - 10} more")
            print()

        # Final verdict
        print("=" * 70)
        if len(self.failures) == 0 and self.total_cycles > 0:
            print("✅ TEST PASSED - No failures detected")
        elif len(self.failures) <= self.total_cycles * 0.05:  # <5% failure rate
            print("✅ TEST PASSED - Failure rate acceptable (<5%)")
        else:
            print("❌ TEST FAILED - Too many failures (>5%)")
        print("=" * 70)
        print()


def main():
    """Main entry point"""
    # Parse command line arguments
    duration_minutes = 60  # Default 1 hour

    if len(sys.argv) > 1:
        try:
            duration_minutes = int(sys.argv[1])
        except ValueError:
            print(f"Invalid duration: {sys.argv[1]}")
            print("Usage: python test_suspend_stability.py [duration_minutes]")
            sys.exit(1)

    # Create and run test
    test = SuspendStabilityTest(duration_minutes=duration_minutes)

    try:
        test.run()
    except Exception as e:
        logger.error(f"Test failed with exception: {e}")
        test.cleanup()
        sys.exit(1)

    sys.exit(0)


if __name__ == '__main__':
    main()
