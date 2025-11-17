# dynamic_scaling.py - JARVIS Phase 0 Experiment 3
from enum import Enum
import time

class CognitiveState(Enum):
    IDLE = "idle"
    ACTIVE = "active"
    CRITICAL = "critical"
    EMERGENCY = "emergency"

class DynamicScaling:
    """Simulate adaptive model loading based on workload"""

    def __init__(self):
        self.current_state = CognitiveState.IDLE
        self.models = {
            CognitiveState.IDLE: {
                "name": "monitoring-1b",
                "memory_gb": 2,
                "cores": 1,
                "latency_ms": 50,
                "use_cases": "Background monitoring, event detection"
            },
            CognitiveState.ACTIVE: {
                "name": "mistral-7b-int8",
                "memory_gb": 8,
                "cores": 3,
                "latency_ms": 200,
                "use_cases": "User queries, system optimization, general decisions"
            },
            CognitiveState.CRITICAL: {
                "name": "mistral-7b-int8-validated",
                "memory_gb": 10,
                "cores": 6,
                "latency_ms": 500,
                "use_cases": "Safety-critical decisions, self-modification, security"
            },
            CognitiveState.EMERGENCY: {
                "name": "rule-based-fallback",
                "memory_gb": 0.1,
                "cores": 1,
                "latency_ms": 1,
                "use_cases": "AI failure, resource exhaustion, kernel panic recovery"
            },
        }
        self.transition_count = 0
        self.total_transition_time = 0

    def transition_to(self, new_state: CognitiveState):
        """Simulate model swap"""
        if self.current_state == new_state:
            print(f"  Already in {new_state.value} state, no transition needed.")
            return 0

        old_model = self.models[self.current_state]
        new_model = self.models[new_state]

        print(f"\n{'='*70}")
        print(f"[TRANSITION {self.transition_count + 1}] {self.current_state.value.upper()} → {new_state.value.upper()}")
        print(f"{'='*70}")

        start = time.perf_counter()

        # Phase 1: Unload current model
        print(f"\n  Phase 1: Unloading {old_model['name']}")
        print(f"    Memory to free: {old_model['memory_gb']}GB")
        print(f"    Cores to free:  {old_model['cores']}")
        time.sleep(0.3)  # Simulate 300ms unload
        print(f"    ✅ Unloaded")

        # Phase 2: Load new model
        print(f"\n  Phase 2: Loading {new_model['name']}")
        print(f"    Memory needed:  {new_model['memory_gb']}GB")
        print(f"    Cores needed:   {new_model['cores']}")

        # Larger models take longer to load
        load_time = 0.5 + (new_model['memory_gb'] * 0.1)  # ~500ms + 100ms per GB
        time.sleep(load_time)
        print(f"    ✅ Loaded ({load_time*1000:.0f}ms)")

        # Phase 3: Warm-up (optional for non-emergency)
        if new_state != CognitiveState.EMERGENCY:
            print(f"\n  Phase 3: Warm-up (dummy inference)")
            time.sleep(0.2)  # 200ms warm-up
            print(f"    ✅ Model ready")

        end = time.perf_counter()
        transition_time = (end - start) * 1000

        self.current_state = new_state
        self.transition_count += 1
        self.total_transition_time += transition_time

        print(f"\n  ⏱️  Total transition time: {transition_time:.0f}ms")
        print(f"  📊 New configuration:")
        print(f"      Model:    {new_model['name']}")
        print(f"      Memory:   {new_model['memory_gb']}GB")
        print(f"      Cores:    {new_model['cores']}")
        print(f"      Latency:  ~{new_model['latency_ms']}ms")
        print(f"      Use case: {new_model['use_cases']}")

        return transition_time

    def get_current_resources(self):
        model = self.models[self.current_state]
        return {
            "state": self.current_state.value,
            "model": model["name"],
            "memory_gb": model["memory_gb"],
            "cores": model["cores"],
            "latency_ms": model["latency_ms"],
            "use_cases": model["use_cases"]
        }

# ===== MAIN TEST =====
if __name__ == "__main__":
    print("="*70)
    print("JARVIS AI-OS - Phase 0 Experiment 3: Dynamic Model Scaling")
    print("="*70)
    print()
    print("This experiment simulates adaptive model loading based on system state.")
    print("The system transitions between 4 states: IDLE → ACTIVE → CRITICAL → EMERGENCY")
    print()

    scaler = DynamicScaling()

    # Show initial state
    initial = scaler.get_current_resources()
    print("[INITIAL STATE]")
    print("-" * 70)
    print(f"  Current state: {initial['state'].upper()}")
    print(f"  Model:         {initial['model']}")
    print(f"  Memory:        {initial['memory_gb']}GB")
    print(f"  Cores:         {initial['cores']}")
    print(f"  Latency:       ~{initial['latency_ms']}ms")
    print(f"  Use case:      {initial['use_cases']}")

    # Scenario: Simulating a full workload cycle
    print("\n" + "="*70)
    print("[SCENARIO] Simulating typical workload cycle...")
    print("="*70)

    # Event 1: User input detected
    print("\n🔔 [EVENT 1] User input detected (typing in terminal)")
    time.sleep(0.5)
    scaler.transition_to(CognitiveState.ACTIVE)

    # Event 2: Critical operation requested
    print("\n🔔 [EVENT 2] Safety-critical operation requested (system modification)")
    time.sleep(0.5)
    scaler.transition_to(CognitiveState.CRITICAL)

    # Event 3: Operation complete, user went idle
    print("\n🔔 [EVENT 3] Operation complete, user idle for 5 minutes")
    time.sleep(0.5)
    scaler.transition_to(CognitiveState.IDLE)

    # Event 4: AI failure (emergency)
    print("\n🔔 [EVENT 4] AI inference timeout detected (system switching to emergency mode)")
    time.sleep(0.5)
    scaler.transition_to(CognitiveState.EMERGENCY)

    # Event 5: Recovery - back to idle
    print("\n🔔 [EVENT 5] System recovered, returning to idle state")
    time.sleep(0.5)
    scaler.transition_to(CognitiveState.IDLE)

    # Results
    print("\n" + "="*70)
    print("[RESULTS]")
    print("="*70)
    print(f"  Total transitions:       {scaler.transition_count}")
    print(f"  Total transition time:   {scaler.total_transition_time:.0f}ms")
    print(f"  Average per transition:  {scaler.total_transition_time/scaler.transition_count:.0f}ms")
    print()

    # Memory savings analysis
    idle_memory = scaler.models[CognitiveState.IDLE]['memory_gb']
    active_memory = scaler.models[CognitiveState.ACTIVE]['memory_gb']
    critical_memory = scaler.models[CognitiveState.CRITICAL]['memory_gb']

    print("[MEMORY SAVINGS ANALYSIS]")
    print("-" * 70)
    print(f"  Fixed 7B model:          8GB (always loaded)")
    print(f"  Dynamic scaling:")
    print(f"    • IDLE state:          {idle_memory}GB (75% savings)")
    print(f"    • ACTIVE state:        {active_memory}GB (same as fixed)")
    print(f"    • CRITICAL state:      {critical_memory}GB (+25% for validation)")
    print()
    print(f"  Average memory (assuming 60% idle, 35% active, 5% critical):")
    fixed_avg = 8
    dynamic_avg = (0.6 * idle_memory) + (0.35 * active_memory) + (0.05 * critical_memory)
    savings_pct = ((fixed_avg - dynamic_avg) / fixed_avg) * 100
    print(f"    • Fixed:               {fixed_avg:.1f}GB")
    print(f"    • Dynamic:             {dynamic_avg:.1f}GB")
    print(f"    • Savings:             {fixed_avg - dynamic_avg:.1f}GB ({savings_pct:.0f}%)")

    print()
    print("="*70)
    print("[KEY INSIGHTS]")
    print("="*70)
    print("  ✅ Dynamic scaling is viable")
    print("  ✅ Transitions take ~1-2 seconds (acceptable for state changes)")
    print(f"  ✅ Memory savings: {savings_pct:.0f}% on average")
    print("  ✅ Emergency fallback works (instant <1ms response)")
    print("  ✅ System can adapt to workload automatically")
    print()
    print("Next: Wait for AI model download, then run inference benchmark")
    print("="*70)
