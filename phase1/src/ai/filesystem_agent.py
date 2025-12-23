#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - FileSystem Agent

Handles file operations, directory management, and permissions.
"""

import os
import time
import subprocess
import shutil
from typing import Dict, Any, Optional
from pathlib import Path
from agent_base import AgentBase
from driver_proxy import get_driver_proxy, read_file_via_driver, write_file_via_driver


class FileSystemAgent(AgentBase):
    """
    FileSystem Agent

    Specializes in:
    - Directory listing
    - File operations (copy, move, remove)
    - File search
    - Permissions management
    - File information
    """

    # Keywords this agent handles
    KEYWORDS = [
        "file", "directory", "folder", "path",
        "ls", "list", "cp", "copy", "mv", "move", "rm", "remove", "delete",
        "chmod", "chown", "permission", "owner",
        "find", "search", "locate",
        "mkdir", "rmdir", "touch", "cat", "read",
        "driver", "block", "disk", "storage"
    ]

    # Action mapping (keyword → action)
    ACTIONS = {
        "ls": "list_directory",
        "list": "list_directory",
        "directory": "list_directory",
        "folder": "list_directory",
        "cp": "copy_file",
        "copy": "copy_file",
        "mv": "move_file",
        "move": "move_file",
        "rename": "move_file",
        "rm": "remove_file",
        "remove": "remove_file",
        "delete": "remove_file",
        "find": "find_file",
        "search": "find_file",
        "locate": "find_file",
        "permission": "show_permissions",
        "chmod": "show_permissions",
        "chown": "show_permissions",
        "owner": "show_permissions",
        "mkdir": "create_directory",
        "touch": "create_file",
        "cat": "read_file",
        "read": "read_file",
        "driver": "show_driver_status",
        "block": "show_driver_status",
        "disk": "show_driver_status",
        "storage": "show_driver_status",
    }

    def __init__(self, model=None, shared_context=None):
        """Initialize FileSystem Agent"""
        super().__init__(
            name="filesystem",
            keywords=self.KEYWORDS,
            model=model,
            shared_context=shared_context
        )
        self.status = "ready"  # No model needed for file operations
        self.driver_proxy = get_driver_proxy()  # Initialize driver proxy

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process filesystem query

        Args:
            query: User query
            context: Optional shared context

        Returns:
            dict: Response with action and result
        """
        start_time = time.time()
        query_lower = query.lower()

        # Determine action from query keywords
        action = self._determine_action(query_lower)

        # Extract path from query
        path = self._extract_path(query)

        # Execute action
        if action == "list_directory":
            result = self._list_directory(path if path else ".")
        elif action == "copy_file":
            # For copy, need source and destination
            result = {"error": "Copy requires source and destination (not implemented in basic query parsing)"}
        elif action == "move_file":
            result = {"error": "Move requires source and destination (not implemented in basic query parsing)"}
        elif action == "remove_file":
            result = {"error": "Remove operations disabled for safety (trust level 3 required)"}
        elif action == "find_file":
            pattern = self._extract_pattern(query)
            result = self._find_file(pattern if pattern else "*", path if path else ".")
        elif action == "show_permissions":
            result = self._show_permissions(path if path else ".")
        elif action == "create_directory":
            result = {"error": "Create directory disabled for safety (trust level 2 required)"}
        elif action == "create_file":
            result = {"error": "Create file disabled for safety (trust level 2 required)"}
        elif action == "read_file":
            result = self._read_file(path if path else "")
        elif action == "show_driver_status":
            result = self._show_driver_status()
        else:
            result = {"error": f"Unknown action: {action}"}

        # Record statistics
        response_time_ms = (time.time() - start_time) * 1000
        success = "error" not in result
        self._record_query(success, response_time_ms, cache_hit=False)

        # Update shared context
        self.update_shared_context()

        # Determine trust level (write operations require approval)
        trust_level = 0  # Read operations
        if action in ["remove_file", "create_directory", "create_file", "copy_file", "move_file"]:
            trust_level = 2  # Write operations require approval

        return self._create_response(
            action=action,
            result=result,
            trust_level=trust_level,
            inference_time_ms=response_time_ms,
            cache_hit=False,
            error=result.get("error")
        )

    def _determine_action(self, query: str) -> str:
        """Determine action from query keywords"""
        for keyword, action in self.ACTIONS.items():
            if keyword in query:
                return action
        return "list_directory"  # Default

    def _extract_path(self, query: str) -> Optional[str]:
        """Extract file path from query"""
        words = query.split()
        for word in words:
            # Check if word looks like a path
            if "/" in word or "\\" in word or word.startswith("."):
                return word.strip("\"'")
        return None

    def _extract_pattern(self, query: str) -> Optional[str]:
        """Extract search pattern from query"""
        words = query.split()
        # Look for quoted pattern or pattern after 'find'/'search'
        for i, word in enumerate(words):
            if word in ["find", "search", "locate"] and i + 1 < len(words):
                return words[i + 1].strip("\"'")
        return None

    def _list_directory(self, path: str) -> Dict[str, Any]:
        """List directory contents"""
        try:
            # Expand path
            expanded_path = os.path.expanduser(path)

            if not os.path.exists(expanded_path):
                return {"error": f"Path not found: {path}"}

            if not os.path.isdir(expanded_path):
                return {"error": f"Not a directory: {path}"}

            # List directory contents
            entries = os.listdir(expanded_path)

            # Separate files and directories
            files = []
            dirs = []

            for entry in sorted(entries):
                entry_path = os.path.join(expanded_path, entry)
                try:
                    if os.path.isdir(entry_path):
                        dirs.append(entry)
                    else:
                        # Get file size
                        size = os.path.getsize(entry_path)
                        files.append({"name": entry, "size": size})
                except (OSError, PermissionError):
                    # Skip entries we can't access
                    continue

            return {
                "path": expanded_path,
                "directories": dirs,
                "files": files,
                "total_dirs": len(dirs),
                "total_files": len(files),
                "summary": f"{len(dirs)} directories, {len(files)} files in {path}"
            }

        except PermissionError:
            return {"error": f"Permission denied: {path}"}
        except Exception as e:
            return {"error": f"Failed to list directory: {str(e)}"}

    def _find_file(self, pattern: str, search_path: str) -> Dict[str, Any]:
        """Find files matching pattern"""
        try:
            # Expand path
            expanded_path = os.path.expanduser(search_path)

            if not os.path.exists(expanded_path):
                return {"error": f"Search path not found: {search_path}"}

            # Use pathlib for pattern matching
            path_obj = Path(expanded_path)
            matches = []

            # Recursive search with pattern
            try:
                if "**" in pattern:
                    # Recursive search
                    matches = list(path_obj.glob(pattern))
                else:
                    # Non-recursive search
                    matches = list(path_obj.glob(pattern))

                # Limit results to prevent overwhelming output
                MAX_RESULTS = 100
                if len(matches) > MAX_RESULTS:
                    truncated = True
                    matches = matches[:MAX_RESULTS]
                else:
                    truncated = False

                # Convert to strings
                match_strings = [str(m.relative_to(path_obj) if m.is_relative_to(path_obj) else m) for m in matches]

                return {
                    "pattern": pattern,
                    "search_path": expanded_path,
                    "matches": match_strings,
                    "count": len(match_strings),
                    "truncated": truncated,
                    "summary": f"Found {len(match_strings)} matches{' (truncated)' if truncated else ''}"
                }

            except PermissionError as e:
                return {"error": f"Permission denied during search: {str(e)}"}

        except Exception as e:
            return {"error": f"Failed to find files: {str(e)}"}

    def _show_permissions(self, path: str) -> Dict[str, Any]:
        """Show file/directory permissions"""
        try:
            # Expand path
            expanded_path = os.path.expanduser(path)

            if not os.path.exists(expanded_path):
                return {"error": f"Path not found: {path}"}

            # Get file stats
            stat_info = os.stat(expanded_path)

            # Get permissions in octal
            permissions_octal = oct(stat_info.st_mode)[-3:]

            # Get permissions in rwx format
            import stat
            mode = stat_info.st_mode
            permissions_str = (
                ('r' if mode & stat.S_IRUSR else '-') +
                ('w' if mode & stat.S_IWUSR else '-') +
                ('x' if mode & stat.S_IXUSR else '-') +
                ('r' if mode & stat.S_IRGRP else '-') +
                ('w' if mode & stat.S_IWGRP else '-') +
                ('x' if mode & stat.S_IXGRP else '-') +
                ('r' if mode & stat.S_IROTH else '-') +
                ('w' if mode & stat.S_IWOTH else '-') +
                ('x' if mode & stat.S_IXOTH else '-')
            )

            return {
                "path": expanded_path,
                "permissions_octal": permissions_octal,
                "permissions_str": permissions_str,
                "owner_uid": stat_info.st_uid,
                "group_gid": stat_info.st_gid,
                "size": stat_info.st_size,
                "modified": stat_info.st_mtime,
                "summary": f"Permissions: {permissions_str} ({permissions_octal})"
            }

        except PermissionError:
            return {"error": f"Permission denied: {path}"}
        except Exception as e:
            return {"error": f"Failed to get permissions: {str(e)}"}

    def _read_file(self, path: str) -> Dict[str, Any]:
        """Read file contents (with size limit for safety)"""
        try:
            if not path:
                return {"error": "No file path specified"}

            # Expand path
            expanded_path = os.path.expanduser(path)

            if not os.path.exists(expanded_path):
                return {"error": f"File not found: {path}"}

            if not os.path.isfile(expanded_path):
                return {"error": f"Not a file: {path}"}

            # Check file size (limit to 1MB for safety)
            MAX_SIZE = 1024 * 1024  # 1MB
            file_size = os.path.getsize(expanded_path)

            if file_size > MAX_SIZE:
                return {"error": f"File too large ({file_size} bytes, max {MAX_SIZE})"}

            # Read file
            try:
                with open(expanded_path, 'r', encoding='utf-8') as f:
                    content = f.read()

                # Truncate if too many lines
                MAX_LINES = 100
                lines = content.split('\n')
                if len(lines) > MAX_LINES:
                    truncated = True
                    content = '\n'.join(lines[:MAX_LINES])
                else:
                    truncated = False

                return {
                    "path": expanded_path,
                    "size": file_size,
                    "lines": len(lines),
                    "content": content,
                    "truncated": truncated,
                    "summary": f"Read {len(lines)} lines{' (truncated)' if truncated else ''}"
                }

            except UnicodeDecodeError:
                return {"error": "File is not text (binary file)"}

        except PermissionError:
            return {"error": f"Permission denied: {path}"}
        except Exception as e:
            return {"error": f"Failed to read file: {str(e)}"}

    def _show_driver_status(self) -> Dict[str, Any]:
        """Show block driver status (Week 23: Driver Framework Integration)"""
        try:
            # Get list of all drivers
            drivers = self.driver_proxy.list_drivers()

            if not drivers:
                return {
                    "drivers": [],
                    "count": 0,
                    "mode": "mock" if self.driver_proxy.mock_mode else "real",
                    "summary": "No block drivers registered"
                }

            # Get info for each driver
            driver_info = []
            for driver_name in drivers:
                info = self.driver_proxy.get_driver_info(driver_name)
                if info:
                    driver_info.append(info)

            return {
                "drivers": driver_info,
                "count": len(driver_info),
                "mode": "mock" if self.driver_proxy.mock_mode else "real",
                "summary": f"Found {len(driver_info)} block driver(s) ({self.driver_proxy.mock_mode and 'mock mode' or 'real drivers'})"
            }

        except Exception as e:
            return {"error": f"Failed to get driver status: {str(e)}"}

    def serialize(self) -> Dict[str, Any]:
        """
        Serialize agent state for suspend/resume (Week 22).

        Returns:
            dict: Agent state including statistics and status
        """
        return {
            'type': 'filesystem_agent',
            'name': self.name,
            'status': getattr(self, 'status', 'ready'),
            'statistics': {
                'total_queries': getattr(self, 'total_queries', 0),
                'successful_queries': getattr(self, 'successful_queries', 0),
                'failed_queries': getattr(self, 'failed_queries', 0),
                'total_response_time_ms': getattr(self, 'total_response_time_ms', 0.0),
                'cache_hits': getattr(self, 'cache_hits', 0)
            },
            'timestamp': time.time()
        }

    def deserialize(self, state: Dict[str, Any]):
        """
        Restore agent state from suspend/resume (Week 22).

        Args:
            state: Serialized agent state
        """
        # Restore status
        self.status = state.get('status', 'ready')

        # Restore statistics
        stats = state.get('statistics', {})
        self.total_queries = stats.get('total_queries', 0)
        self.successful_queries = stats.get('successful_queries', 0)
        self.failed_queries = stats.get('failed_queries', 0)
        self.total_response_time_ms = stats.get('total_response_time_ms', 0.0)
        self.cache_hits = stats.get('cache_hits', 0)


if __name__ == "__main__":
    # Test FileSystem Agent
    print("="*70)
    print("Testing FileSystem Agent")
    print("="*70)

    agent = FileSystemAgent()
    print(f"\nAgent: {agent}")
    print(f"Status: {agent.get_status()}")

    # Test queries
    test_queries = [
        "list files in .",
        "list files in /tmp",
        "find *.py in .",
        "show permissions for .",
        "read file agent_base.py",
        "show driver status",  # Week 23: Driver integration test
    ]

    for query in test_queries:
        print(f"\n{'='*70}")
        print(f"Query: {query}")
        print("="*70)

        response = agent.process_query(query)

        print(f"Agent: {response['agent']}")
        print(f"Action: {response['action']}")
        print(f"Trust Level: {response['trust_level']}")
        print(f"Response Time: {response['inference_time_ms']:.2f}ms")

        if "error" in response:
            print(f"Error: {response['error']}")
        else:
            result = response['result']
            if 'summary' in result:
                print(f"Summary: {result['summary']}")
            else:
                print(f"Result: {result}")

    # Final status
    print(f"\n{'='*70}")
    print("Final Agent Status")
    print("="*70)
    status = agent.get_status()
    print(f"Queries Processed: {status['statistics']['total_queries']}")
    print(f"Success Rate: {status['statistics']['success_rate']*100:.1f}%")
    print(f"Avg Response Time: {status['statistics']['avg_response_time_ms']:.2f}ms")
    print("\nFileSystem Agent test complete!")
