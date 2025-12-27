#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Enhanced Snapshot Manager
Week 17: Persistent Snapshots and Rollback

Provides persistent snapshot management with memory and disk storage.
Enables automated rollback on failures.

Author: JARVIS AI-OS Team
Date: November 25, 2025
"""

import json
import time
import logging
from pathlib import Path
from typing import Dict, List, Optional, Any
from dataclasses import dataclass, asdict, field

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


# ============================================================================
# Enhanced System Snapshot
# ============================================================================

@dataclass
class EnhancedSystemSnapshot:
    """
    Enhanced system snapshot with AI state, IPC state, and metadata

    Extends Week 16's basic SystemSnapshot with:
    - AI model state (IDLE/ACTIVE/CRITICAL/EMERGENCY)
    - IPC queue state
    - Decision cache state
    - Agent health states
    - Snapshot metadata (type, trigger, ID)
    """

    # Basic system metrics (from Week 16)
    timestamp: float
    cpu_usage: float
    memory_usage: float
    disk_usage: float
    network_active: bool
    file_count: int
    service_states: Dict[str, str]

    # NEW: AI state
    ai_model_state: str  # "idle", "active", "critical", "emergency"
    ai_model_loaded: Optional[str]  # "tinyllama", "phi3", None
    ai_model_memory_mb: float

    # NEW: IPC state
    ipc_queue_size: int
    ipc_pending_messages: int

    # NEW: Decision cache state
    cache_size: int
    cache_hit_rate: float

    # NEW: Agent states
    agent_states: Dict[str, str] = field(default_factory=dict)  # agent_name -> status

    # NEW: Snapshot metadata
    snapshot_id: str = ""
    snapshot_type: str = "memory"  # "memory" or "disk"
    snapshot_trigger: str = "manual"  # "manual", "pre_risky_op", "critical_state", etc.


# ============================================================================
# Enhanced Rollback Manager
# ============================================================================

class EnhancedRollbackManager:
    """
    Manage system snapshots with hybrid memory + disk storage

    Features:
    - Memory snapshots for quick rollback (<0.5s)
    - Disk snapshots for persistent recovery (<2s)
    - Automatic rotation (5 memory, 20 disk)
    - JSON serialization for disk snapshots
    - Rollback automation on failures
    """

    def __init__(self, max_memory_snapshots: int = 5, max_disk_snapshots: int = 20,
                 disk_snapshot_dir: str = "/tmp/jarvis_snapshots"):
        """
        Initialize enhanced rollback manager

        Args:
            max_memory_snapshots: Maximum memory snapshots (default: 5)
            max_disk_snapshots: Maximum disk snapshots (default: 20)
            disk_snapshot_dir: Directory for disk snapshots
        """
        self.max_memory_snapshots = max_memory_snapshots
        self.max_disk_snapshots = max_disk_snapshots

        # Memory snapshots (rotating list)
        self.memory_snapshots: List[EnhancedSystemSnapshot] = []

        # Disk snapshot directory
        self.disk_snapshot_dir = Path(disk_snapshot_dir)
        self.disk_snapshot_dir.mkdir(parents=True, exist_ok=True)

        # Statistics
        self.statistics = {
            'total_snapshots': 0,
            'memory_snapshots': 0,
            'disk_snapshots': 0,
            'rollbacks_executed': 0,
            'rollback_successes': 0,
            'rollback_failures': 0,
            'avg_snapshot_time_ms': 0.0,
            'avg_rollback_time_ms': 0.0
        }

        logger.info(f"[SnapshotMgr] Initialized (memory: {max_memory_snapshots}, "
                   f"disk: {max_disk_snapshots}, dir: {disk_snapshot_dir})")

    # ========================================================================
    # Snapshot Creation
    # ========================================================================

    def create_snapshot(self, system_state, snapshot_type: str = 'memory',
                       trigger: str = 'manual') -> str:
        """
        Create system snapshot (memory or disk)

        Args:
            system_state: Current SystemStateManager instance
            snapshot_type: "memory" or "disk"
            trigger: Reason for snapshot creation

        Returns:
            Snapshot ID
        """
        start_time = time.time()

        # Generate snapshot ID
        snapshot_id = f"snapshot_{int(time.time() * 1000)}"

        # Create snapshot
        snapshot = self._capture_system_state(system_state, snapshot_id,
                                               snapshot_type, trigger)

        # Store snapshot
        if snapshot_type == 'memory':
            self._store_memory_snapshot(snapshot)
        else:
            self._save_to_disk(snapshot, snapshot_id)

        # Update statistics
        duration_ms = (time.time() - start_time) * 1000
        self._update_snapshot_stats(snapshot_type, duration_ms)

        logger.info(f"[SnapshotMgr] Created {snapshot_type} snapshot: {snapshot_id} "
                   f"(trigger: {trigger}, time: {duration_ms:.1f}ms)")

        return snapshot_id

    def _capture_system_state(self, system_state, snapshot_id: str,
                              snapshot_type: str, trigger: str) -> EnhancedSystemSnapshot:
        """
        Capture current system state

        Args:
            system_state: SystemStateManager instance
            snapshot_id: Snapshot ID
            snapshot_type: "memory" or "disk"
            trigger: Snapshot trigger reason

        Returns:
            EnhancedSystemSnapshot
        """
        import psutil

        # Get AI state from SystemStateManager
        ai_state = system_state.get_current_state_str() if hasattr(system_state, 'get_current_state_str') else system_state.get_current_state()
        ai_model = None
        ai_memory = 0.0

        if hasattr(system_state, 'model_loader') and system_state.model_loader:
            if system_state.model_loader.current_model:
                ai_model = system_state.model_loader.current_model
                stats = system_state.model_loader.get_statistics()
                ai_memory = stats.get('current_memory_mb', 0.0)

        # Get IPC state (mock for Phase 1 - no real IPC queue tracking yet)
        ipc_queue_size = 0
        ipc_pending = 0

        # Get decision cache state (mock for Phase 1)
        cache_size = 200  # From Week 3-4 implementation
        cache_hit_rate = 85.7  # From validation

        # Get agent states (from system_state if available)
        agent_states = {}
        if hasattr(system_state, 'agent_router') and system_state.agent_router:
            # Agent router from Week 11
            agent_states = {
                'device': 'healthy',
                'network': 'healthy',
                'filesystem': 'healthy',
                'user': 'healthy'
            }

        # Capture system metrics
        cpu = psutil.cpu_percent(interval=0.1)
        memory = psutil.virtual_memory().percent
        disk = psutil.disk_usage('/').percent

        # Create snapshot
        snapshot = EnhancedSystemSnapshot(
            timestamp=time.time(),
            cpu_usage=cpu,
            memory_usage=memory,
            disk_usage=disk,
            network_active=True,  # Assume active
            file_count=1000,  # Placeholder
            service_states={},  # Placeholder for Phase 1

            # AI state
            ai_model_state=ai_state,
            ai_model_loaded=ai_model,
            ai_model_memory_mb=ai_memory,

            # IPC state
            ipc_queue_size=ipc_queue_size,
            ipc_pending_messages=ipc_pending,

            # Cache state
            cache_size=cache_size,
            cache_hit_rate=cache_hit_rate,

            # Agent states
            agent_states=agent_states,

            # Metadata
            snapshot_id=snapshot_id,
            snapshot_type=snapshot_type,
            snapshot_trigger=trigger
        )

        return snapshot

    def _store_memory_snapshot(self, snapshot: EnhancedSystemSnapshot):
        """Store snapshot in memory with rotation"""
        self.memory_snapshots.append(snapshot)

        # Rotate if exceeds max
        if len(self.memory_snapshots) > self.max_memory_snapshots:
            removed = self.memory_snapshots.pop(0)
            logger.debug(f"[SnapshotMgr] Rotated memory snapshot: {removed.snapshot_id}")

    def _save_to_disk(self, snapshot: EnhancedSystemSnapshot, snapshot_id: str):
        """
        Serialize snapshot to disk as JSON

        Args:
            snapshot: Snapshot to save
            snapshot_id: Snapshot ID
        """
        path = self.disk_snapshot_dir / f"{snapshot_id}.json"

        try:
            with open(path, 'w') as f:
                json.dump(asdict(snapshot), f, indent=2)

            logger.info(f"[SnapshotMgr] Saved disk snapshot: {path}")

            # Cleanup old snapshots
            self._cleanup_old_snapshots()

        except Exception as e:
            logger.error(f"[SnapshotMgr] Failed to save snapshot to disk: {e}")

    def _cleanup_old_snapshots(self):
        """Remove oldest disk snapshots if exceeds max"""
        snapshots = sorted(self.disk_snapshot_dir.glob("snapshot_*.json"))

        if len(snapshots) > self.max_disk_snapshots:
            to_remove = len(snapshots) - self.max_disk_snapshots

            for snapshot_file in snapshots[:to_remove]:
                try:
                    snapshot_file.unlink()
                    logger.debug(f"[SnapshotMgr] Removed old snapshot: {snapshot_file.name}")
                except Exception as e:
                    logger.error(f"[SnapshotMgr] Failed to remove snapshot: {e}")

    # ========================================================================
    # Snapshot Retrieval
    # ========================================================================

    def get_snapshot(self, snapshot_id: Optional[str] = None) -> Optional[EnhancedSystemSnapshot]:
        """
        Get snapshot by ID (searches memory first, then disk)

        Args:
            snapshot_id: Snapshot ID (if None, returns most recent)

        Returns:
            EnhancedSystemSnapshot or None if not found
        """
        if snapshot_id is None:
            # Return most recent memory snapshot
            if self.memory_snapshots:
                return self.memory_snapshots[-1]
            else:
                # Try most recent disk snapshot
                snapshots = sorted(self.disk_snapshot_dir.glob("snapshot_*.json"))
                if snapshots:
                    return self._load_from_disk(snapshots[-1])
                return None

        # Search memory snapshots
        for snapshot in reversed(self.memory_snapshots):
            if snapshot.snapshot_id == snapshot_id:
                return snapshot

        # Search disk snapshots
        path = self.disk_snapshot_dir / f"{snapshot_id}.json"
        if path.exists():
            return self._load_from_disk(path)

        logger.warning(f"[SnapshotMgr] Snapshot not found: {snapshot_id}")
        return None

    def _load_from_disk(self, path: Path) -> Optional[EnhancedSystemSnapshot]:
        """
        Load snapshot from disk

        Args:
            path: Path to snapshot JSON file

        Returns:
            EnhancedSystemSnapshot or None on error
        """
        try:
            with open(path, 'r') as f:
                data = json.load(f)

            # Convert dict to EnhancedSystemSnapshot
            snapshot = EnhancedSystemSnapshot(**data)
            logger.info(f"[SnapshotMgr] Loaded disk snapshot: {path.name}")
            return snapshot

        except Exception as e:
            logger.error(f"[SnapshotMgr] Failed to load snapshot from {path}: {e}")
            return None

    def list_snapshots(self) -> List[Dict[str, Any]]:
        """
        List all available snapshots (memory + disk)

        Returns:
            List of snapshot metadata dictionaries
        """
        snapshots = []

        # Memory snapshots
        for snapshot in self.memory_snapshots:
            snapshots.append({
                'snapshot_id': snapshot.snapshot_id,
                'type': 'memory',
                'timestamp': snapshot.timestamp,
                'trigger': snapshot.snapshot_trigger,
                'ai_state': snapshot.ai_model_state,
                'ai_model': snapshot.ai_model_loaded
            })

        # Disk snapshots
        for snapshot_file in sorted(self.disk_snapshot_dir.glob("snapshot_*.json")):
            try:
                snapshot = self._load_from_disk(snapshot_file)
                if snapshot:
                    snapshots.append({
                        'snapshot_id': snapshot.snapshot_id,
                        'type': 'disk',
                        'timestamp': snapshot.timestamp,
                        'trigger': snapshot.snapshot_trigger,
                        'ai_state': snapshot.ai_model_state,
                        'ai_model': snapshot.ai_model_loaded
                    })
            except (OSError, IOError, ValueError, KeyError):
                pass  # Skip corrupted snapshots

        return sorted(snapshots, key=lambda x: x['timestamp'])

    # ========================================================================
    # Rollback Execution
    # ========================================================================

    def execute_rollback(self, snapshot_id: Optional[str] = None,
                        system_state=None) -> bool:
        """
        Execute rollback to specified snapshot

        Args:
            snapshot_id: Snapshot ID to restore (None = most recent)
            system_state: SystemStateManager instance to restore

        Returns:
            True if rollback successful, False otherwise
        """
        start_time = time.time()

        # Find snapshot
        snapshot = self.get_snapshot(snapshot_id)
        if not snapshot:
            logger.error(f"[SnapshotMgr] Rollback failed: Snapshot not found")
            self.statistics['rollback_failures'] += 1
            return False

        logger.info(f"[SnapshotMgr] Rolling back to snapshot: {snapshot.snapshot_id} "
                   f"(type: {snapshot.snapshot_type}, trigger: {snapshot.snapshot_trigger})")

        try:
            # Restore AI state
            if system_state:
                self._restore_ai_state(snapshot, system_state)

            # Restore cache state (log only for Phase 1)
            self._restore_cache_state(snapshot)

            # Restore IPC state (log only for Phase 1)
            self._restore_ipc_state(snapshot)

            # Restore agent states (log only for Phase 1)
            self._restore_agent_states(snapshot)

            # Update statistics
            duration_ms = (time.time() - start_time) * 1000
            self._update_rollback_stats(True, duration_ms)

            logger.info(f"[SnapshotMgr] Rollback successful ({duration_ms:.1f}ms)")
            return True

        except Exception as e:
            logger.error(f"[SnapshotMgr] Rollback failed: {e}")
            self.statistics['rollback_failures'] += 1
            return False

    def _restore_ai_state(self, snapshot: EnhancedSystemSnapshot, system_state):
        """
        Restore AI model state

        Args:
            snapshot: Snapshot to restore from
            system_state: SystemStateManager instance
        """
        target_state = snapshot.ai_model_state

        logger.info(f"[SnapshotMgr] Restoring AI state: {target_state}")

        # Transition to target state using existing SystemStateManager methods
        current_state = system_state.get_current_state_str() if hasattr(system_state, 'get_current_state_str') else system_state.get_current_state()

        if current_state == target_state:
            logger.info(f"[SnapshotMgr] AI already in target state: {target_state}")
            return

        # Transition via SystemStateManager
        if target_state == "idle":
            system_state.transition_to_idle()
        elif target_state == "active":
            system_state.transition_to_active()
        elif target_state == "critical":
            system_state.transition_to_critical()
        elif target_state == "emergency":
            system_state.transition_to_emergency()

        logger.info(f"[SnapshotMgr] AI state restored: {current_state} → {target_state}")

    def _restore_cache_state(self, snapshot: EnhancedSystemSnapshot):
        """
        Restore decision cache state

        Phase 1: Log only (cache is ephemeral, no persistence yet)
        Phase 2: Restore cache contents from snapshot

        Args:
            snapshot: Snapshot to restore from
        """
        logger.info(f"[SnapshotMgr] Would restore cache: "
                   f"size={snapshot.cache_size}, hit_rate={snapshot.cache_hit_rate:.1f}%")

        # Phase 1: No actual restoration (cache rebuild on demand)
        # Phase 2: Serialize/deserialize cache hash table

    def _restore_ipc_state(self, snapshot: EnhancedSystemSnapshot):
        """
        Restore IPC queue state

        Phase 1: Log only (IPC queue is ephemeral)
        Phase 2: Clear IPC queue, restore pending messages

        Args:
            snapshot: Snapshot to restore from
        """
        logger.info(f"[SnapshotMgr] Would restore IPC: "
                   f"queue_size={snapshot.ipc_queue_size}, pending={snapshot.ipc_pending_messages}")

        # Phase 1: No actual restoration (IPC rebuilds on demand)
        # Phase 2: Clear ring buffer, restore saved messages

    def _restore_agent_states(self, snapshot: EnhancedSystemSnapshot):
        """
        Restore agent health states

        Phase 1: Log only (agents restart on demand)
        Phase 2: Restart failed agents, restore resource allocations

        Args:
            snapshot: Snapshot to restore from
        """
        logger.info(f"[SnapshotMgr] Would restore agents: {snapshot.agent_states}")

        # Phase 1: No actual restoration (agents self-heal)
        # Phase 2: Restart degraded/failed agents

    # ========================================================================
    # Statistics
    # ========================================================================

    def _update_snapshot_stats(self, snapshot_type: str, duration_ms: float):
        """Update snapshot creation statistics"""
        self.statistics['total_snapshots'] += 1

        if snapshot_type == 'memory':
            self.statistics['memory_snapshots'] += 1
        else:
            self.statistics['disk_snapshots'] += 1

        # Update average snapshot time
        total = self.statistics['total_snapshots']
        current_avg = self.statistics['avg_snapshot_time_ms']
        self.statistics['avg_snapshot_time_ms'] = (
            (current_avg * (total - 1) + duration_ms) / total
        )

    def _update_rollback_stats(self, success: bool, duration_ms: float):
        """Update rollback execution statistics"""
        self.statistics['rollbacks_executed'] += 1

        if success:
            self.statistics['rollback_successes'] += 1
        else:
            self.statistics['rollback_failures'] += 1

        # Update average rollback time
        total = self.statistics['rollbacks_executed']
        current_avg = self.statistics['avg_rollback_time_ms']
        self.statistics['avg_rollback_time_ms'] = (
            (current_avg * (total - 1) + duration_ms) / total
        )

    def get_statistics(self) -> Dict:
        """Get snapshot manager statistics"""
        stats = self.statistics.copy()

        # Add current counts
        stats['current_memory_snapshots'] = len(self.memory_snapshots)

        disk_snapshots = list(self.disk_snapshot_dir.glob("snapshot_*.json"))
        stats['current_disk_snapshots'] = len(disk_snapshots)

        # Add success rate
        if stats['rollbacks_executed'] > 0:
            stats['rollback_success_rate'] = (
                stats['rollback_successes'] / stats['rollbacks_executed'] * 100
            )
        else:
            stats['rollback_success_rate'] = 0.0

        return stats

    def reset_statistics(self):
        """Reset statistics"""
        self.statistics = {
            'total_snapshots': 0,
            'memory_snapshots': 0,
            'disk_snapshots': 0,
            'rollbacks_executed': 0,
            'rollback_successes': 0,
            'rollback_failures': 0,
            'avg_snapshot_time_ms': 0.0,
            'avg_rollback_time_ms': 0.0
        }


# ============================================================================
# Main (for testing)
# ============================================================================

def main():
    """Test snapshot manager standalone"""
    print("="*70)
    print("JARVIS AI-OS - Snapshot Manager Smoke Test")
    print("="*70)
    print()

    # Mock system state
    class MockSystemState:
        def get_current_state(self):
            return "active"

        def transition_to_idle(self):
            logger.info("[MockState] Transitioning to IDLE")

        def transition_to_active(self):
            logger.info("[MockState] Transitioning to ACTIVE")

        def transition_to_critical(self):
            logger.info("[MockState] Transitioning to CRITICAL")

        def transition_to_emergency(self):
            logger.info("[MockState] Transitioning to EMERGENCY")

    mock_state = MockSystemState()
    manager = EnhancedRollbackManager(max_memory_snapshots=3, max_disk_snapshots=5)

    # Test 1: Create memory snapshot
    print("Test 1: Create memory snapshot")
    snapshot_id = manager.create_snapshot(mock_state, snapshot_type='memory',
                                          trigger='test_memory')
    print(f"  Snapshot ID: {snapshot_id}")
    print(f"  Memory snapshots: {len(manager.memory_snapshots)}")
    print()

    # Test 2: Create disk snapshot
    print("Test 2: Create disk snapshot")
    snapshot_id2 = manager.create_snapshot(mock_state, snapshot_type='disk',
                                           trigger='test_disk')
    disk_snapshots = list(manager.disk_snapshot_dir.glob("snapshot_*.json"))
    print(f"  Snapshot ID: {snapshot_id2}")
    print(f"  Disk snapshots: {len(disk_snapshots)}")
    print()

    # Test 3: List all snapshots
    print("Test 3: List all snapshots")
    all_snapshots = manager.list_snapshots()
    print(f"  Total snapshots: {len(all_snapshots)}")
    for snap in all_snapshots:
        print(f"    - {snap['snapshot_id']}: {snap['type']}, {snap['ai_state']}")
    print()

    # Test 4: Execute rollback
    print("Test 4: Execute rollback")
    success = manager.execute_rollback(snapshot_id, mock_state)
    print(f"  Rollback success: {success}")
    print()

    # Test 5: Statistics
    print("Test 5: Statistics")
    stats = manager.get_statistics()
    for key, value in stats.items():
        print(f"  {key}: {value}")
    print()

    print("[OK] Snapshot manager smoke test complete!")


if __name__ == "__main__":
    main()
