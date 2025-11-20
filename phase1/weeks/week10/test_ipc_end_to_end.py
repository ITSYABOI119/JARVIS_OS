#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Week 10 End-to-End IPC Test

Tests Python ↔ seL4 communication via shared memory IPC while seL4 is running in QEMU.

Test Flow:
1. Launch seL4 JARVIS in QEMU (background process)
2. Python connects via /dev/shm shared memory
3. Send test queries to seL4
4. Verify responses from decision cache
5. Measure round-trip latency
6. Report results

Prerequisites:
- seL4 built and ready in ~/jarvis-phase1/hello-world_build
- IPC client working (from Week 8)
- QEMU available

Usage:
    # Terminal 1 (WSL): This test will launch QEMU automatically
    python3 test_ipc_end_to_end.py

    # Or manually:
    # Terminal 1: cd ~/jarvis-phase1/hello-world_build && ./simulate
    # Terminal 2: python3 test_ipc_end_to_end.py --no-launch
"""

import sys
import os
import time
import subprocess
import signal
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'src' / 'ai'))

try:
    from ipc_client import IPCClient, MessageType
except ImportError:
    print("ERROR: Cannot import IPCClient. Make sure phase1/src/ai/ipc_client.py exists.")
    sys.exit(1)


class Colors:
    """ANSI color codes for terminal output"""
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'


def print_header(msg):
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*70}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{msg:^70}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*70}{Colors.ENDC}\n")


def print_test(msg):
    print(f"{Colors.OKBLUE}▶ {msg}{Colors.ENDC}")


def print_pass(msg):
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")


def print_fail(msg):
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}")


def print_info(msg):
    print(f"{Colors.OKCYAN}  {msg}{Colors.ENDC}")


def launch_qemu_background():
    """
    Launch seL4 JARVIS in QEMU as background process

    Returns:
        subprocess.Popen: QEMU process object
    """
    print_test("Launching seL4 JARVIS in QEMU...")

    # Check if we're in WSL or native Linux
    try:
        result = subprocess.run(['wsl', '--list'], capture_output=True, text=True)
        is_windows = result.returncode == 0
    except FileNotFoundError:
        is_windows = False

    if is_windows:
        # Running on Windows - use WSL
        cmd = [
            'wsl', 'bash', '-c',
            'cd ~/jarvis-phase1/hello-world_build && ./simulate'
        ]
    else:
        # Native Linux
        cmd = ['bash', '-c', 'cd ~/jarvis-phase1/hello-world_build && ./simulate']

    try:
        # Launch QEMU in background
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid if not is_windows else None
        )

        # Give QEMU time to boot
        print_info("Waiting for QEMU to boot (10 seconds)...")
        time.sleep(10)

        # Check if process is still running
        if proc.poll() is not None:
            print_fail("QEMU process exited unexpectedly!")
            stdout, stderr = proc.communicate()
            print(f"STDOUT: {stdout[:500]}")
            print(f"STDERR: {stderr[:500]}")
            return None

        print_pass(f"QEMU launched (PID: {proc.pid})")
        return proc

    except Exception as e:
        print_fail(f"Failed to launch QEMU: {e}")
        return None


def cleanup_qemu(proc):
    """Terminate QEMU process"""
    if proc and proc.poll() is None:
        print_test("Terminating QEMU...")
        try:
            # Send Ctrl+A X sequence to QEMU to exit gracefully
            proc.terminate()
            proc.wait(timeout=5)
            print_pass("QEMU terminated")
        except subprocess.TimeoutExpired:
            print_info("Force killing QEMU...")
            proc.kill()
            proc.wait()


def test_ipc_connection():
    """Test 1: IPC Connection"""
    print_test("TEST 1: IPC Connection to seL4")

    try:
        client = IPCClient()
        print_pass("IPC client created")
        print_info(f"  Shared memory: {client.shm_name}")
        print_info(f"  Ring buffer: {client.ring_size} slots")
        print_info(f"  Message size: {client.message_size} bytes")
        return client
    except Exception as e:
        print_fail(f"Failed to create IPC client: {e}")
        return None


def test_send_query(client, query_text):
    """Test 2: Send Query to seL4"""
    print_test(f"TEST 2: Send Query '{query_text}'")

    try:
        client.send(MessageType.QUERY, query_text)
        print_pass(f"Query sent: {query_text}")
        return True
    except Exception as e:
        print_fail(f"Failed to send query: {e}")
        return False


def test_receive_response(client, timeout=5.0):
    """Test 3: Receive Response from seL4"""
    print_test(f"TEST 3: Receive Response (timeout={timeout}s)")

    start_time = time.time()

    try:
        # Poll for response
        while time.time() - start_time < timeout:
            response = client.receive()
            if response:
                elapsed = (time.time() - start_time) * 1000  # ms
                print_pass(f"Response received in {elapsed:.2f}ms")

                # Parse response
                msg_type, msg_id, payload = response
                print_info(f"  Type: {msg_type}")
                print_info(f"  ID: {msg_id}")
                print_info(f"  Payload: {payload}")

                # Check if it's a cache response (ACTION:xxx|TRUST:x|HIT:x)
                if "ACTION:" in payload:
                    parts = payload.split('|')
                    action = parts[0].split(':')[1] if len(parts) > 0 else "unknown"
                    trust = parts[1].split(':')[1] if len(parts) > 1 else "?"
                    hit = parts[2].split(':')[1] if len(parts) > 2 else "?"

                    print_info(f"  ➜ Action: {action}")
                    print_info(f"  ➜ Trust: {trust}")
                    print_info(f"  ➜ Cache Hit: {hit}")

                return payload

            time.sleep(0.1)  # 100ms poll interval

        print_fail(f"No response received within {timeout}s")
        return None

    except Exception as e:
        print_fail(f"Failed to receive response: {e}")
        return None


def test_multiple_queries(client):
    """Test 4: Multiple Query/Response Cycle"""
    print_test("TEST 4: Multiple Query/Response Cycle")

    test_queries = [
        "help",
        "status",
        "cache stats",
        "ls",
        "git status"
    ]

    results = {
        'sent': 0,
        'received': 0,
        'cache_hits': 0,
        'cache_misses': 0,
        'latencies': []
    }

    for query in test_queries:
        print_info(f"\nQuery: '{query}'")

        # Send query
        try:
            start = time.time()
            client.send(MessageType.QUERY, query)
            results['sent'] += 1

            # Wait for response
            time.sleep(0.5)  # Give seL4 time to process
            response = client.receive()

            if response:
                latency = (time.time() - start) * 1000  # ms
                results['received'] += 1
                results['latencies'].append(latency)

                payload = response[2]
                if "HIT:1" in payload:
                    results['cache_hits'] += 1
                    print_info(f"  ✓ Cache HIT ({latency:.2f}ms)")
                else:
                    results['cache_misses'] += 1
                    print_info(f"  ○ Cache MISS ({latency:.2f}ms)")
            else:
                print_info(f"  ✗ No response")

        except Exception as e:
            print_info(f"  ✗ Error: {e}")

    # Summary
    print()
    print_info("Results:")
    print_info(f"  Sent: {results['sent']}")
    print_info(f"  Received: {results['received']}")
    print_info(f"  Cache Hits: {results['cache_hits']}")
    print_info(f"  Cache Misses: {results['cache_misses']}")

    if results['latencies']:
        avg_latency = sum(results['latencies']) / len(results['latencies'])
        min_latency = min(results['latencies'])
        max_latency = max(results['latencies'])
        print_info(f"  Avg Latency: {avg_latency:.2f}ms")
        print_info(f"  Min Latency: {min_latency:.2f}ms")
        print_info(f"  Max Latency: {max_latency:.2f}ms")

    success_rate = (results['received'] / results['sent'] * 100) if results['sent'] > 0 else 0

    if success_rate == 100:
        print_pass(f"All queries successful ({success_rate:.0f}%)")
    elif success_rate >= 80:
        print_info(f"Most queries successful ({success_rate:.0f}%)")
    else:
        print_fail(f"Low success rate ({success_rate:.0f}%)")

    return results


def main():
    """Main test execution"""
    print_header("JARVIS AI-OS - Week 10 End-to-End IPC Test")

    # Check if we should launch QEMU
    auto_launch = '--no-launch' not in sys.argv

    qemu_proc = None

    try:
        # Launch QEMU if requested
        if auto_launch:
            qemu_proc = launch_qemu_background()
            if not qemu_proc:
                print_fail("Failed to launch QEMU. Exiting.")
                return 1
        else:
            print_info("Skipping QEMU launch (--no-launch flag)")
            print_info("Make sure seL4 is running in another terminal!")
            time.sleep(2)

        # Test 1: Connection
        client = test_ipc_connection()
        if not client:
            return 1

        # Test 2: Send query
        if not test_send_query(client, "help"):
            return 1

        # Test 3: Receive response
        response = test_receive_response(client, timeout=5.0)
        if not response:
            print_fail("No response from seL4. Check if QEMU is running and IPC handler is active.")
            return 1

        # Test 4: Multiple queries
        results = test_multiple_queries(client)

        # Final summary
        print_header("End-to-End IPC Test Complete")

        if results['received'] == results['sent']:
            print_pass(f"✓ All tests passed! ({results['received']}/{results['sent']} responses)")
            print_pass(f"✓ Cache hit rate: {results['cache_hits']}/{results['sent']} ({results['cache_hits']/results['sent']*100:.1f}%)")
            return 0
        else:
            print_fail(f"Some tests failed ({results['received']}/{results['sent']} responses)")
            return 1

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        return 1
    except Exception as e:
        print_fail(f"Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        # Cleanup
        if qemu_proc:
            cleanup_qemu(qemu_proc)


if __name__ == "__main__":
    exit_code = main()
    sys.exit(exit_code)
