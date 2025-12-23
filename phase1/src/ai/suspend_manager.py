#!/usr/bin/env python3
"""
Suspend/Resume Manager for JARVIS AI-OS

Coordinates graceful suspend and resume of all system components.
Saves component states to disk and restores them on resume.

Week 22: ACPI S3 Suspend/Resume (Phase 1 PoC)
"""

from enum import Enum
from typing import Dict, Optional, Any, List
import json
import time
import logging
from pathlib import Path
from datetime import datetime


class SuspendState(Enum):
    """Suspend/resume states"""
    RUNNING = "running"
    SUSPENDING = "suspending"
    SUSPENDED = "suspended"
    RESUMING = "resuming"
    FAILED = "failed"


class SuspendManager:
    """
    Coordinates suspend/resume across all system components.

    Manages state persistence for:
    - System state manager
    - AI agents (Device, Network, FileSystem, User)
    - Agent lifecycle manager

    State files saved to /tmp/jarvis_state/:
    - metadata.json - Component list, timestamps
    - <component>_state.json - Per-component state
    """

    def __init__(self, state_dir: str = "/tmp/jarvis_state"):
        """
        Initialize suspend manager.

        Args:
            state_dir: Directory to save/load state files
        """
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)

        self.state = SuspendState.RUNNING
        self.logger = logging.getLogger(__name__)

        # Component registry (name -> component with serialize/deserialize)
        self.components: Dict[str, Any] = {}

        # Timing metrics
        self.suspend_start_time: float = 0.0
        self.suspend_end_time: float = 0.0
        self.resume_start_time: float = 0.0
        self.resume_end_time: float = 0.0

        # Statistics
        self.total_suspends = 0
        self.total_resumes = 0
        self.successful_suspends = 0
        self.successful_resumes = 0
        self.failed_suspends = 0
        self.failed_resumes = 0

        self.logger.info(f"SuspendManager initialized (state_dir: {self.state_dir})")

    def register_component(self, name: str, component: Any) -> bool:
        """
        Register component that needs state persistence.

        Component must implement:
        - serialize() -> Dict[str, Any]
        - deserialize(state: Dict[str, Any]) -> None

        Args:
            name: Component name (e.g., "device_agent")
            component: Component instance

        Returns:
            True if registered successfully, False if missing methods
        """
        # Validate component has required methods
        if not hasattr(component, 'serialize'):
            self.logger.error(f"Component {name} missing serialize() method")
            return False

        if not hasattr(component, 'deserialize'):
            self.logger.error(f"Component {name} missing deserialize() method")
            return False

        self.components[name] = component
        self.logger.info(f"Registered component: {name}")
        return True

    def unregister_component(self, name: str) -> bool:
        """
        Unregister component.

        Args:
            name: Component name

        Returns:
            True if unregistered, False if not found
        """
        if name in self.components:
            del self.components[name]
            self.logger.info(f"Unregistered component: {name}")
            return True
        return False

    def suspend(self) -> bool:
        """
        Suspend all components and save state to disk.

        Target: <5 seconds

        Process:
        1. Transition to SUSPENDING state
        2. Serialize each component
        3. Save to JSON files
        4. Save metadata
        5. Transition to SUSPENDED state

        Returns:
            True if successful, False if any errors
        """
        self.suspend_start_time = time.time()
        self.total_suspends += 1
        self.state = SuspendState.SUSPENDING

        self.logger.info("=" * 60)
        self.logger.info("SUSPEND INITIATED")
        self.logger.info("=" * 60)

        try:
            # Prepare metadata
            metadata = {
                'version': '1.0',
                'timestamp': time.time(),
                'datetime': datetime.now().isoformat(),
                'components': list(self.components.keys()),
                'state': 'suspended',
                'suspend_duration': 0.0  # Updated later
            }

            # Suspend components in reverse dependency order
            # (User agents first, system components last)
            component_order = list(self.components.keys())

            for idx, name in enumerate(reversed(component_order), 1):
                self.logger.info(f"[{idx}/{len(component_order)}] Suspending: {name}")

                component = self.components[name]

                # Serialize component state
                start_time = time.time()
                try:
                    state_data = component.serialize()
                    serialize_time = time.time() - start_time

                    # Validate state data is JSON-serializable
                    if not isinstance(state_data, dict):
                        raise TypeError(f"serialize() must return dict, got {type(state_data)}")

                    # Save to disk
                    state_file = self.state_dir / f"{name}_state.json"
                    with open(state_file, 'w') as f:
                        json.dump(state_data, f, indent=2)

                    file_size = state_file.stat().st_size

                    self.logger.info(
                        f"  ✓ {name}: {file_size} bytes, "
                        f"serialized in {serialize_time:.3f}s"
                    )

                except Exception as e:
                    self.logger.error(f"  ✗ {name} serialization failed: {e}")
                    raise

            # Update metadata with suspend duration
            self.suspend_end_time = time.time()
            metadata['suspend_duration'] = self.suspend_end_time - self.suspend_start_time

            # Save metadata last
            metadata_file = self.state_dir / "metadata.json"
            with open(metadata_file, 'w') as f:
                json.dump(metadata, f, indent=2)

            suspend_time = self.suspend_end_time - self.suspend_start_time

            self.logger.info("=" * 60)
            self.logger.info(f"SUSPEND COMPLETE ({suspend_time:.3f}s)")
            self.logger.info(f"  Components: {len(self.components)}")
            self.logger.info(f"  State dir: {self.state_dir}")
            self.logger.info("=" * 60)

            self.state = SuspendState.SUSPENDED
            self.successful_suspends += 1
            return True

        except Exception as e:
            self.suspend_end_time = time.time()
            suspend_time = self.suspend_end_time - self.suspend_start_time

            self.logger.error("=" * 60)
            self.logger.error(f"SUSPEND FAILED ({suspend_time:.3f}s)")
            self.logger.error(f"  Error: {e}")
            self.logger.error("=" * 60)

            self.state = SuspendState.FAILED
            self.failed_suspends += 1
            return False

    def resume(self) -> bool:
        """
        Resume all components and restore state from disk.

        Target: <15 seconds

        Process:
        1. Transition to RESUMING state
        2. Load metadata
        3. Deserialize each component (in dependency order)
        4. Transition to RUNNING state

        Returns:
            True if successful, False if any errors
        """
        self.resume_start_time = time.time()
        self.total_resumes += 1
        self.state = SuspendState.RESUMING

        self.logger.info("=" * 60)
        self.logger.info("RESUME INITIATED")
        self.logger.info("=" * 60)

        try:
            # Load metadata
            metadata_file = self.state_dir / "metadata.json"
            if not metadata_file.exists():
                raise FileNotFoundError(f"No suspend state found at {metadata_file}")

            with open(metadata_file, 'r') as f:
                metadata = json.load(f)

            self.logger.info(f"  State from: {metadata['datetime']}")
            self.logger.info(f"  Components: {len(metadata['components'])}")

            # Resume components in dependency order
            # (System components first, user agents last)
            component_order = metadata['components']

            for idx, name in enumerate(component_order, 1):
                if name not in self.components:
                    self.logger.warning(
                        f"[{idx}/{len(component_order)}] Component {name} not registered, skipping"
                    )
                    continue

                self.logger.info(f"[{idx}/{len(component_order)}] Resuming: {name}")

                component = self.components[name]

                # Load state from disk
                state_file = self.state_dir / f"{name}_state.json"
                if not state_file.exists():
                    self.logger.warning(f"  ⚠ {name}: No state file found, skipping")
                    continue

                start_time = time.time()
                try:
                    with open(state_file, 'r') as f:
                        state_data = json.load(f)

                    # Restore component state
                    component.deserialize(state_data)
                    deserialize_time = time.time() - start_time

                    file_size = state_file.stat().st_size

                    self.logger.info(
                        f"  ✓ {name}: {file_size} bytes, "
                        f"deserialized in {deserialize_time:.3f}s"
                    )

                except Exception as e:
                    self.logger.error(f"  ✗ {name} deserialization failed: {e}")
                    # Continue with other components (partial recovery)

            self.resume_end_time = time.time()
            resume_time = self.resume_end_time - self.resume_start_time

            self.logger.info("=" * 60)
            self.logger.info(f"RESUME COMPLETE ({resume_time:.3f}s)")
            self.logger.info(f"  Components restored: {len(component_order)}")
            self.logger.info("=" * 60)

            self.state = SuspendState.RUNNING
            self.successful_resumes += 1
            return True

        except Exception as e:
            self.resume_end_time = time.time()
            resume_time = self.resume_end_time - self.resume_start_time

            self.logger.error("=" * 60)
            self.logger.error(f"RESUME FAILED ({resume_time:.3f}s)")
            self.logger.error(f"  Error: {e}")
            self.logger.error("=" * 60)

            self.state = SuspendState.FAILED
            self.failed_resumes += 1
            return False

    def get_metrics(self) -> Dict[str, Any]:
        """
        Get suspend/resume timing metrics and statistics.

        Returns:
            Dictionary of metrics including:
            - Current state
            - Component list
            - Timing metrics
            - Success/failure counts
        """
        last_suspend_time = (
            self.suspend_end_time - self.suspend_start_time
            if self.suspend_start_time > 0 else 0.0
        )

        last_resume_time = (
            self.resume_end_time - self.resume_start_time
            if self.resume_start_time > 0 else 0.0
        )

        return {
            'state': self.state.value,
            'components': list(self.components.keys()),
            'component_count': len(self.components),
            'last_suspend_time': last_suspend_time,
            'last_resume_time': last_resume_time,
            'total_suspends': self.total_suspends,
            'total_resumes': self.total_resumes,
            'successful_suspends': self.successful_suspends,
            'successful_resumes': self.successful_resumes,
            'failed_suspends': self.failed_suspends,
            'failed_resumes': self.failed_resumes,
            'state_dir': str(self.state_dir)
        }

    def get_state_size(self) -> int:
        """
        Get total size of saved state files in bytes.

        Returns:
            Total size in bytes, 0 if no state files exist
        """
        total_size = 0

        if not self.state_dir.exists():
            return 0

        for state_file in self.state_dir.glob("*.json"):
            total_size += state_file.stat().st_size

        return total_size

    def clear_state(self) -> bool:
        """
        Clear all saved state files (for testing/cleanup).

        Returns:
            True if cleared successfully
        """
        try:
            if self.state_dir.exists():
                for state_file in self.state_dir.glob("*.json"):
                    state_file.unlink()
                    self.logger.debug(f"Deleted: {state_file}")

                self.logger.info(f"Cleared state directory: {self.state_dir}")
            return True
        except Exception as e:
            self.logger.error(f"Failed to clear state: {e}")
            return False

    def __repr__(self) -> str:
        """String representation"""
        return (
            f"SuspendManager(state={self.state.value}, "
            f"components={len(self.components)}, "
            f"state_dir={self.state_dir})"
        )
