#!/usr/bin/env python3
"""
JARVIS AI-OS - Power Manager

Week 44: Host-side thermal monitoring and power management via UART IPC.
Uses the existing UARTIPCClient to send shell commands (temp, watchdog, reboot)
to the seL4 kernel running on the Pi 4.

Thermal Thresholds:
  - WARN:      70C - log warning
  - CRITICAL:  80C - log critical, increase monitoring frequency
  - EMERGENCY: 85C - trigger reboot

Usage:
  python power_manager.py --port COM3 --monitor          # Continuous monitoring
  python power_manager.py --port COM3 --temp             # One-shot temperature
  python power_manager.py --port COM3 --watchdog-status  # Watchdog status
  python power_manager.py --port COM3 --reboot           # Reboot Pi 4
"""

import sys
import os
import time
import argparse
import re
import logging

# Add parent directory for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from uart_ipc_client import UARTIPCClient

# Thermal thresholds (degrees C)
THRESHOLD_WARN = 70
THRESHOLD_CRITICAL = 80
THRESHOLD_EMERGENCY = 85

# Monitoring defaults
DEFAULT_INTERVAL = 30  # seconds between polls
CRITICAL_INTERVAL = 5  # seconds when above critical threshold

logger = logging.getLogger("power_manager")


class PowerManager:
    """Host-side power management for JARVIS Pi 4."""

    def __init__(self, client: UARTIPCClient):
        self.client = client
        self.last_temp = None
        self.max_temp_seen = 0
        self.read_count = 0
        self.warn_count = 0
        self.critical_count = 0

    def get_temperature(self):
        """
        Read current SoC temperature.

        Returns:
            Float temperature in degrees C, or None on error.
        """
        result = self.client.send_command("temp")
        if not result:
            logger.warning("No response to temp command")
            return None

        # Parse "Temperature: XX.XC" format
        match = re.search(r'(\d+)\.(\d+)C', result)
        if match:
            temp = float(f"{match.group(1)}.{match.group(2)}")
            self.last_temp = temp
            self.read_count += 1
            if temp > self.max_temp_seen:
                self.max_temp_seen = temp
            return temp

        # Might be an error message
        logger.warning(f"Could not parse temperature from: {result.strip()}")
        return None

    def get_watchdog_status(self):
        """
        Read watchdog timer status.

        Returns:
            Status string, or None on error.
        """
        return self.client.send_command("watchdog")

    def reboot(self):
        """
        Trigger Pi 4 reboot via watchdog.

        Returns:
            Response string (may not arrive if reboot is immediate).
        """
        logger.warning("Sending reboot command to Pi 4")
        return self.client.send_command("reboot")

    def check_thresholds(self, temp):
        """Check temperature against thresholds and take action."""
        if temp >= THRESHOLD_EMERGENCY:
            logger.critical(f"EMERGENCY: {temp}C >= {THRESHOLD_EMERGENCY}C - triggering reboot!")
            self.critical_count += 1
            self.reboot()
            return "emergency"
        elif temp >= THRESHOLD_CRITICAL:
            logger.critical(f"CRITICAL: {temp}C >= {THRESHOLD_CRITICAL}C")
            self.critical_count += 1
            return "critical"
        elif temp >= THRESHOLD_WARN:
            logger.warning(f"WARNING: {temp}C >= {THRESHOLD_WARN}C")
            self.warn_count += 1
            return "warn"
        return "ok"

    def monitor(self, interval=DEFAULT_INTERVAL):
        """
        Continuous temperature monitoring loop.

        Args:
            interval: Seconds between temperature polls.
        """
        logger.info(f"Starting thermal monitor (interval={interval}s)")
        logger.info(f"Thresholds: WARN={THRESHOLD_WARN}C "
                     f"CRITICAL={THRESHOLD_CRITICAL}C "
                     f"EMERGENCY={THRESHOLD_EMERGENCY}C")

        current_interval = interval

        try:
            while True:
                temp = self.get_temperature()
                if temp is not None:
                    level = self.check_thresholds(temp)

                    # Adaptive polling: faster when critical
                    if level == "critical":
                        current_interval = CRITICAL_INTERVAL
                    elif level == "ok":
                        current_interval = interval

                    logger.info(f"Temp: {temp}C (max seen: {self.max_temp_seen}C, "
                                f"reads: {self.read_count})")
                else:
                    logger.warning("Temperature read failed")

                time.sleep(current_interval)

        except KeyboardInterrupt:
            logger.info("Monitor stopped by user")
            self.print_summary()

    def print_summary(self):
        """Print monitoring session summary."""
        print(f"\n--- Power Manager Summary ---")
        print(f"Temperature readings: {self.read_count}")
        print(f"Max temperature seen: {self.max_temp_seen}C")
        print(f"Warning events:  {self.warn_count}")
        print(f"Critical events: {self.critical_count}")
        print(f"Last temperature: {self.last_temp}C")


def main():
    parser = argparse.ArgumentParser(description="JARVIS Power Manager")
    parser.add_argument("--port", default="COM3",
                        help="Serial port (default: COM3)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--temp", action="store_true",
                        help="Read temperature once and exit")
    parser.add_argument("--watchdog-status", action="store_true",
                        help="Show watchdog status and exit")
    parser.add_argument("--reboot", action="store_true",
                        help="Reboot Pi 4 and exit")
    parser.add_argument("--monitor", action="store_true",
                        help="Continuous temperature monitoring")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL,
                        help=f"Monitor interval in seconds (default: {DEFAULT_INTERVAL})")
    parser.add_argument("--mock", action="store_true",
                        help="Use mock mode (no hardware)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose logging")
    args = parser.parse_args()

    # Setup logging
    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=level,
                        format="%(asctime)s [%(levelname)s] %(message)s")

    # Connect to Pi 4
    if args.mock:
        client = UARTIPCClient(port=args.port, baud_rate=args.baud, mock=True)
    else:
        client = UARTIPCClient(port=args.port, baud_rate=args.baud)

    if not client.connect():
        logger.error("Failed to connect to Pi 4")
        sys.exit(1)

    pm = PowerManager(client)

    try:
        if args.temp:
            temp = pm.get_temperature()
            if temp is not None:
                print(f"Temperature: {temp}C")
            else:
                print("Failed to read temperature")
                sys.exit(1)

        elif args.watchdog_status:
            status = pm.get_watchdog_status()
            if status:
                print(status.strip())
            else:
                print("Failed to read watchdog status")
                sys.exit(1)

        elif args.reboot:
            result = pm.reboot()
            if result:
                print(result.strip())
            print("Reboot command sent")

        elif args.monitor:
            pm.monitor(interval=args.interval)

        else:
            # Default: show temperature and watchdog status
            temp = pm.get_temperature()
            status = pm.get_watchdog_status()
            if temp is not None:
                print(f"Temperature: {temp}C")
            if status:
                print(status.strip())

    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
