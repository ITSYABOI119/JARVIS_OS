#!/usr/bin/env python3
"""
Test Logger - Structured logging for integration tests

Provides three-tier logging:
1. Console output (INFO level) - Real-time progress
2. Detailed logs (DEBUG level) - All events to timestamped files
3. Summary reports (JSON format) - Automated analysis

Usage:
    logger = TestLogger('e2e_integration')
    logger.info("Test started")
    logger.debug("Detailed information")
    logger.error("Something failed")
    logger.save_summary({'result': 'PASS'})
"""

import logging
import json
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, Any, Optional


class TestLogger:
    """Structured logging for integration tests"""

    def __init__(self, test_name: str, log_dir: Optional[str] = None):
        """
        Initialize test logger

        Args:
            test_name: Name of the test (e.g., 'e2e_integration', 'stability_12h')
            log_dir: Directory for log files (default: integration_tests/logs)
        """
        self.test_name = test_name
        self.start_time = datetime.now()
        self.timestamp = self.start_time.strftime('%Y%m%d_%H%M%S')

        # Set up log directory
        if log_dir is None:
            # Default to integration_tests/logs
            script_dir = Path(__file__).parent.parent
            log_dir = script_dir / 'logs'
        else:
            log_dir = Path(log_dir)

        log_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir = log_dir

        # Log file paths
        self.log_file = log_dir / f"{test_name}_{self.timestamp}.log"
        self.json_file = log_dir / f"{test_name}_{self.timestamp}.json"

        # Set up Python logger
        self.logger = logging.getLogger(f"integration_tests.{test_name}")
        self.logger.setLevel(logging.DEBUG)
        self.logger.handlers.clear()  # Clear any existing handlers

        # Console handler (INFO level)
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(logging.INFO)
        console_formatter = logging.Formatter(
            '[%(asctime)s] [%(levelname)s] %(message)s',
            datefmt='%H:%M:%S'
        )
        console_handler.setFormatter(console_formatter)
        self.logger.addHandler(console_handler)

        # File handler (DEBUG level)
        file_handler = logging.FileHandler(self.log_file, encoding='utf-8')
        file_handler.setLevel(logging.DEBUG)
        file_formatter = logging.Formatter(
            '[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(file_formatter)
        self.logger.addHandler(file_handler)

        # Summary data (for JSON export)
        self.summary = {
            'test_name': test_name,
            'start_time': self.start_time.isoformat(),
            'log_file': str(self.log_file),
            'events': []
        }

        self.info(f"Test logger initialized: {test_name}")
        self.info(f"Log file: {self.log_file}")

    def debug(self, message: str, **kwargs):
        """Log debug message"""
        self.logger.debug(message)
        self._add_event('DEBUG', message, kwargs)

    def info(self, message: str, **kwargs):
        """Log info message"""
        self.logger.info(message)
        self._add_event('INFO', message, kwargs)

    def warning(self, message: str, **kwargs):
        """Log warning message"""
        self.logger.warning(message)
        self._add_event('WARNING', message, kwargs)

    def error(self, message: str, **kwargs):
        """Log error message"""
        self.logger.error(message)
        self._add_event('ERROR', message, kwargs)

    def critical(self, message: str, **kwargs):
        """Log critical message"""
        self.logger.critical(message)
        self._add_event('CRITICAL', message, kwargs)

    def _add_event(self, level: str, message: str, data: Dict[str, Any]):
        """Add event to summary"""
        event = {
            'timestamp': datetime.now().isoformat(),
            'level': level,
            'message': message
        }
        if data:
            event['data'] = data

        self.summary['events'].append(event)

    def log_metric(self, name: str, value: Any, unit: str = ''):
        """
        Log a metric value

        Args:
            name: Metric name (e.g., 'ipc_latency_median')
            value: Metric value
            unit: Optional unit (e.g., 'us', 'ms', '%')
        """
        if 'metrics' not in self.summary:
            self.summary['metrics'] = {}

        self.summary['metrics'][name] = {
            'value': value,
            'unit': unit,
            'timestamp': datetime.now().isoformat()
        }

        unit_str = f" {unit}" if unit else ""
        self.info(f"Metric: {name} = {value}{unit_str}")

    def log_result(self, component: str, status: str, details: Optional[Dict] = None):
        """
        Log test result for a component

        Args:
            component: Component name (e.g., 'qemu_boot', 'agent_loading')
            status: Result status ('PASS', 'FAIL', 'SKIP')
            details: Optional details dictionary
        """
        if 'results' not in self.summary:
            self.summary['results'] = {}

        result = {
            'status': status,
            'timestamp': datetime.now().isoformat()
        }
        if details:
            result['details'] = details

        self.summary['results'][component] = result

        status_icon = {
            'PASS': '[PASS]',
            'FAIL': '[FAIL]',
            'SKIP': '[SKIP]'
        }.get(status, '[????]')

        self.info(f"{status_icon} {component}")
        if details:
            for key, value in details.items():
                self.debug(f"  {key}: {value}")

    def save_summary(self, additional_data: Optional[Dict] = None):
        """
        Save summary to JSON file

        Args:
            additional_data: Optional additional data to include in summary
        """
        # Add end time
        end_time = datetime.now()
        self.summary['end_time'] = end_time.isoformat()
        self.summary['duration_seconds'] = (end_time - self.start_time).total_seconds()

        # Add additional data
        if additional_data:
            self.summary.update(additional_data)

        # Calculate overall status
        if 'results' in self.summary:
            all_passed = all(
                r['status'] == 'PASS'
                for r in self.summary['results'].values()
                if r['status'] != 'SKIP'
            )
            self.summary['overall_status'] = 'PASS' if all_passed else 'FAIL'

        # Write JSON file
        with open(self.json_file, 'w', encoding='utf-8') as f:
            json.dump(self.summary, f, indent=2)

        self.info(f"Summary saved to: {self.json_file}")

        return self.summary

    def print_summary(self):
        """Print summary to console"""
        print("\n" + "=" * 70)
        print(f"TEST SUMMARY: {self.test_name}")
        print("=" * 70)

        if 'results' in self.summary:
            print("\nResults:")
            for component, result in self.summary['results'].items():
                status = result['status']
                icon = {'PASS': '[PASS]', 'FAIL': '[FAIL]', 'SKIP': '[SKIP]'}.get(status, '[????]')
                print(f"  {icon} {component}")

        if 'metrics' in self.summary:
            print("\nMetrics:")
            for name, metric in self.summary['metrics'].items():
                value = metric['value']
                unit = metric.get('unit', '')
                unit_str = f" {unit}" if unit else ""
                print(f"  {name}: {value}{unit_str}")

        if 'overall_status' in self.summary:
            overall = self.summary['overall_status']
            print("\n" + "=" * 70)
            print(f"Overall Status: {overall}")
            print("=" * 70)

        duration = self.summary.get('duration_seconds', 0)
        print(f"\nDuration: {duration:.1f} seconds")
        print(f"Log file: {self.log_file}")
        print(f"JSON file: {self.json_file}\n")

    def close(self):
        """Close logger and save summary"""
        self.save_summary()
        self.print_summary()

        # Close handlers
        for handler in self.logger.handlers[:]:
            handler.close()
            self.logger.removeHandler(handler)


class ProgressLogger:
    """Simple progress logger for long-running operations"""

    def __init__(self, total: int, logger: TestLogger, description: str = "Progress"):
        """
        Initialize progress logger

        Args:
            total: Total number of items
            logger: TestLogger instance
            description: Progress description
        """
        self.total = total
        self.logger = logger
        self.description = description
        self.current = 0
        self.last_percent = -1

    def update(self, increment: int = 1):
        """Update progress"""
        self.current += increment
        percent = int((self.current / self.total) * 100)

        # Log every 10%
        if percent >= self.last_percent + 10:
            self.logger.info(f"{self.description}: {self.current}/{self.total} ({percent}%)")
            self.last_percent = percent

    def finish(self):
        """Mark progress as complete"""
        self.logger.info(f"{self.description}: Complete ({self.total}/{self.total})")


# Example usage
if __name__ == "__main__":
    # Create logger
    logger = TestLogger('example_test')

    # Log various events
    logger.info("Starting test")
    logger.debug("Detailed debug information")

    # Log metrics
    logger.log_metric('ipc_latency_median', 54.2, 'us')
    logger.log_metric('cache_hit_rate', 85.7, '%')

    # Log results
    logger.log_result('component_1', 'PASS', {'time': 1.23})
    logger.log_result('component_2', 'PASS', {'time': 2.34})
    logger.log_result('component_3', 'FAIL', {'error': 'Timeout'})

    # Progress example
    progress = ProgressLogger(100, logger, "Processing items")
    for i in range(100):
        # Simulate work
        import time
        time.sleep(0.01)
        progress.update()
    progress.finish()

    # Save and close
    logger.close()

    print(f"\nExample log file created: {logger.log_file}")
    print(f"Example JSON file created: {logger.json_file}")
