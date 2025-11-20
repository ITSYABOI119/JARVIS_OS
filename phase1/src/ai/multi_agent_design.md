# JARVIS Multi-Agent Architecture Design

**Version:** 1.0
**Date:** November 20, 2025
**Status:** Week 11 Initial Design

---

## Overview

JARVIS uses a multi-agent architecture with specialist agents coordinated by a central router. This design distributes workload, improves response time, and enables domain-specific decision-making.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        User / Shell                          │
└─────────────────────────────────┬───────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────┐
│                      Agent Router                            │
│  - Keyword-based routing                                     │
│  - Shared context management                                 │
│  - Query preprocessing                                       │
└─────────────────────┬───────────────────────────────────────┘
                      │
        ┌─────────────┼─────────────┬─────────────┐
        │             │             │             │
        ▼             ▼             ▼             ▼
┌─────────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│   Device    │ │ Network  │ │FileSystem│ │   User   │
│   Manager   │ │  Agent   │ │  Agent   │ │Interaction│
│   Agent     │ │          │ │          │ │  Agent   │
└─────────────┘ └──────────┘ └──────────┘ └──────────┘
        │             │             │             │
        └─────────────┼─────────────┴─────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                   Shared Context Pool                        │
│  - System state (memory, CPU, disk)                          │
│  - Active operations                                         │
│  - Decision cache reference                                  │
│  - IPC statistics                                            │
└─────────────────────────────────────────────────────────────┘
```

---

## Agent Roles

### 1. Device Manager Agent

**Domain:** Hardware operations, device management, system resources

**Keywords:**
- device, hardware, disk, usb, driver
- memory, ram, cpu, processor
- storage, partition, mount
- thermal, temperature, battery

**Actions:**
- `show_disk_space` - Display disk usage (df)
- `show_memory` - Display RAM usage (free)
- `show_cpu` - Display CPU info (lscpu)
- `list_devices` - List USB/PCI devices (lsusb, lspci)
- `show_thermal` - Display temperatures (sensors)
- `show_battery` - Display battery status (acpi)

**Model:** Phi-3 Mini 3.8B (or TinyLlama 1.1B for efficiency)

**Example queries:**
- "How much disk space is left?"
- "Show me memory usage"
- "List USB devices"
- "What's the CPU temperature?"

---

### 2. Network Agent

**Domain:** Network operations, connectivity, remote access

**Keywords:**
- network, ping, ssh, http, connection
- ip, port, interface, wifi, ethernet
- router, gateway, dns, firewall

**Actions:**
- `show_network_status` - Display network interfaces (ip addr)
- `ping_host` - Ping remote host (ping)
- `check_connection` - Test connectivity
- `show_listening_ports` - Display open ports (netstat)
- `show_routes` - Display routing table (ip route)

**Model:** Phi-3 Mini 3.8B (or TinyLlama 1.1B for efficiency)

**Example queries:**
- "Ping google.com"
- "Show network status"
- "What ports are listening?"
- "Check internet connection"

---

### 3. FileSystem Agent

**Domain:** File operations, directory management, permissions

**Keywords:**
- file, directory, folder
- ls, cp, mv, rm, chmod, chown
- find, search, locate
- permissions, owner

**Actions:**
- `list_directory` - List directory contents (ls)
- `copy_file` - Copy file (cp)
- `move_file` - Move/rename file (mv)
- `remove_file` - Delete file (rm)
- `find_file` - Search for file (find)
- `show_permissions` - Display file permissions (ls -l)

**Model:** Phi-3 Mini 3.8B (or TinyLlama 1.1B for efficiency)

**Example queries:**
- "List files in /tmp"
- "Copy README.md to /backup"
- "Find all .txt files"
- "What are the permissions on config.json?"

---

### 4. User Interaction Agent

**Domain:** General queries, help, status, cache operations

**Keywords:**
- help, status, cache, agent
- show, display, tell, what, how
- (default fallback for unmatched queries)

**Actions:**
- `show_help` - Display help message
- `show_status` - Display system status
- `show_cache_stats` - Display cache statistics
- `show_agent_status` - Display agent health
- `general_query` - Answer general questions

**Model:** Phi-3 Mini 3.8B (primary user-facing agent, needs best quality)

**Example queries:**
- "help"
- "What's the system status?"
- "Show cache statistics"
- "How do I use JARVIS?"

---

## Routing Algorithm

### Keyword-Based Routing

**Priority:** Device > Network > FileSystem > User (default)

**Algorithm:**
```python
def route_query(query: str) -> Agent:
    query_lower = query.lower()

    # Priority 1: Device keywords
    if any(kw in query_lower for kw in DEVICE_KEYWORDS):
        return device_agent

    # Priority 2: Network keywords
    if any(kw in query_lower for kw in NETWORK_KEYWORDS):
        return network_agent

    # Priority 3: FileSystem keywords
    if any(kw in query_lower for kw in FILESYSTEM_KEYWORDS):
        return filesystem_agent

    # Default: User Interaction Agent
    return user_agent
```

**Routing overhead:** <5ms (simple keyword matching)

**Expected accuracy:** >90% (validated in Phase 0)

---

## Shared Context Pool

### Context Structure

```python
class SharedContext:
    # System state (updated every 1s)
    memory_used: int        # MB
    memory_total: int       # MB
    cpu_percent: float      # 0-100
    disk_used: int          # GB
    disk_total: int         # GB

    # Active operations (thread-safe)
    active_operations: List[str]  # ["copying file", "pinging host"]

    # Cache reference (read-only)
    cache_hit_rate: float   # 0-1.0
    cache_lookups: int

    # IPC statistics (read-only)
    ipc_sent: int
    ipc_received: int
    ipc_drops: int

    # Agent health (updated by each agent)
    agent_status: Dict[str, str]  # {"device": "healthy", ...}

    # Timestamp
    last_update: float  # Unix timestamp
```

### Access Pattern

- **Read:** Lock-free (RCU pattern, validated in Phase 0)
- **Write:** Atomic updates only
- **Update frequency:** 1s for system state, immediate for operations

**Performance:**
- Read access: <0.1ms (atomic read)
- Write access: <1ms (atomic write)
- Update overhead: <5ms (system state polling)

---

## Communication Protocol

### Query Format

**Python → Router:**
```python
query = {
    "text": "show disk space",
    "timestamp": 1700000000.0,
    "user_id": "default"
}
```

**Router → Agent:**
```python
agent_query = {
    "text": "show disk space",
    "context": shared_context.to_dict(),
    "timestamp": 1700000000.0,
    "agent": "device"
}
```

### Response Format

**Agent → Router:**
```python
response = {
    "agent": "device",
    "action": "show_disk_space",
    "result": {
        "filesystem": "/dev/sda1",
        "size": "100G",
        "used": "45G",
        "available": "50G",
        "use_percent": "45%"
    },
    "trust_level": 0,  # 0=automatic, 1=notify, 2=request, 3=require
    "inference_time_ms": 64.5,
    "cache_hit": False
}
```

**Router → Python:**
```python
final_response = {
    "text": "Disk space: 50G available (45% used)",
    "action": "show_disk_space",
    "trust_level": 0,
    "agent": "device",
    "inference_time_ms": 64.5,
    "routing_time_ms": 2.3
}
```

---

## Multi-Agent Queries

### Coordination Example

**Query:** "Copy /tmp/data.txt to remote server 192.168.1.10"

**Routing:**
1. Router detects both FileSystem ("copy") and Network ("remote server") keywords
2. Route to FileSystem agent first (primary operation)
3. FileSystem agent checks if file exists
4. FileSystem agent requests Network agent to check connectivity
5. Network agent pings 192.168.1.10, returns status
6. FileSystem agent executes `scp /tmp/data.txt user@192.168.1.10:`
7. Both agents update shared context with operation status

**Coordination protocol:**
```python
# FileSystem agent
def handle_remote_copy(file_path, remote_host):
    # Check file exists
    if not os.path.exists(file_path):
        return {"error": "File not found"}

    # Request network check (via shared context)
    shared_context.request_operation("network", "check_connectivity", remote_host)

    # Wait for network agent response (max 5s timeout)
    if wait_for_response("network", timeout=5.0):
        # Execute copy
        return execute_scp(file_path, remote_host)
    else:
        return {"error": "Network check timeout"}
```

---

## Agent Base Class

### Common Interface

```python
class AgentBase:
    def __init__(self, name: str, model_path: str, context: SharedContext):
        self.name = name
        self.model = load_model(model_path)
        self.context = context
        self.ipc_client = None  # Optional
        self.statistics = AgentStatistics()

    def process_query(self, query: str, context: dict) -> dict:
        """Process a query and return response"""
        raise NotImplementedError

    def get_status(self) -> dict:
        """Return agent health status"""
        return {
            "name": self.name,
            "status": "healthy",
            "queries_processed": self.statistics.total_queries,
            "avg_response_time_ms": self.statistics.avg_response_time
        }

    def update_context(self):
        """Update shared context with agent-specific info"""
        self.context.agent_status[self.name] = self.get_status()
```

---

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| **Routing overhead** | <5ms | Keyword matching |
| **Context access** | <1ms | Lock-free read |
| **Single-agent response** | <200ms | AI inference + execution |
| **Multi-agent response** | <300ms | Coordination overhead |
| **Routing accuracy** | >90% | Correct agent selected |
| **Agent load time** | <3s | All 4 agents |

---

## Testing Strategy

### Unit Tests (per agent)
- 5+ test queries per agent
- Verify correct actions returned
- Response time <200ms

### Integration Tests
- Router: 20+ routing tests (>90% accuracy)
- Context: 5+ sharing tests
- Multi-agent: 10+ coordination tests

### Performance Tests
- Benchmark routing overhead
- Benchmark context access
- Benchmark agent response time

---

## Implementation Plan

### Week 11 Schedule

**Task 1 (2-3h):** Design (this document)
**Task 2 (4-6h):** Implement 4 specialist agents
**Task 3 (3-4h):** Implement agent router + shared context
**Task 4 (4-5h):** Multi-agent testing
**Task 5 (3-4h):** seL4 integration (optional)

**Total:** 16-22 hours estimated, 8-10 hours expected (based on Weeks 5-10 efficiency)

---

## Future Enhancements

### Week 12: Conflict Resolution
- Priority-based arbitration
- Resource allocation
- Deadlock detection

### Week 13-14: Dynamic Model Scaling
- IDLE state: TinyLlama 1.1B for all agents
- ACTIVE state: Phi-3 Mini 3.8B for active agent, TinyLlama for others
- CRITICAL state: Phi-3 Mini + validator

### Week 15-18: SHIELD Integration
- Per-agent trust levels
- Cross-agent validation
- Shadow execution for high-risk operations

---

## References

**Phase 0 Validation:** `phase0/experiments/multi_agent_orchestration.py`
**Phase 1 Plan:** `phase1/docs/PHASE_1_IMPLEMENTATION_PLAN.md` (Weeks 10-12)
**JARVIS Architecture:** `ARCHITECTURE_ENHANCEMENTS.md` (Shared Context Pool)

---

**Status:** Design complete, ready for implementation
**Next:** Begin Task 2 (Implement Specialist Agents)
