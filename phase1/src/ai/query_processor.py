#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Query Processing Pipeline
Week 6: Query normalization, cache integration, command parsing

This module handles:
1. Query normalization (lowercase, whitespace, trimming)
2. Cache lookup (check decision cache before AI)
3. Command parsing (AI response → kernel command)
4. End-to-end query processing

Architecture:
  User Query → Normalize → Hash → Cache Lookup → (Cache Hit | AI Inference) → Parse → Command
"""

import hashlib
import json
import re
from typing import Dict, Optional, Tuple
import logging

# ============================================================================
# Constants
# ============================================================================

# FNV-1a hash constants (64-bit, matching C implementation)
FNV_64_PRIME = 0x100000001b3
FNV_64_OFFSET_BASIS = 0xcbf29ce484222325

# Command types (must match seL4 kernel)
COMMAND_TYPES = {
    'READ_CPU_STATS': 0,
    'READ_MEMORY_STATS': 1,
    'READ_DISK_STATS': 2,
    'LIST_PROCESSES': 3,
    'SHOW_HELP': 4,
    'SHOW_STATUS': 5,
    'NETWORK_STATUS': 6,
    'SYSTEM_INFO': 7,
    'UPTIME': 8,
    'TEMPERATURE': 9,
}

# Trust levels for safety
TRUST_LEVELS = {
    'AUTOMATIC': 0,      # Execute immediately (read-only)
    'NOTIFY': 1,         # Execute + notify user
    'REQUEST': 2,        # Require user permission
    'REQUIRE': 3,        # Explicit approval required
}

# ============================================================================
# Query Normalization
# ============================================================================

class QueryNormalizer:
    """
    Normalizes user queries for cache lookup

    Normalization steps:
    1. Convert to lowercase
    2. Collapse multiple spaces to single space
    3. Trim leading/trailing whitespace
    4. Remove special characters (optional)
    """

    def __init__(self):
        self.logger = logging.getLogger('QueryNormalizer')

    def normalize(self, query: str) -> str:
        """
        Normalize a user query

        Args:
            query (str): Raw user input

        Returns:
            str: Normalized query

        Examples:
            "  SHOW   CPU  " → "show cpu"
            "What's\\t\\tthe\\nmemory?" → "what's the memory?"
            "   help   " → "help"
        """
        if not query:
            return ""

        # 1. Convert to lowercase
        normalized = query.lower()

        # 2. Collapse multiple whitespace to single space
        normalized = ' '.join(normalized.split())

        # 3. Trim leading/trailing whitespace
        normalized = normalized.strip()

        return normalized

    def extract_keywords(self, query: str) -> list:
        """
        Extract keywords from query for analysis

        Args:
            query (str): Normalized query

        Returns:
            list: List of keywords (excluding common words)
        """
        # Common words to exclude
        stopwords = {'a', 'an', 'the', 'is', 'are', 'was', 'were',
                     'what', 'when', 'where', 'who', 'how', 'can',
                     'me', 'my', 'show', 'get', 'check', 'tell'}

        words = query.split()
        keywords = [word for word in words if word not in stopwords and len(word) > 2]

        return keywords

# ============================================================================
# Hash Function (FNV-1a, matching C implementation)
# ============================================================================

def fnv1a_hash_64(data: str) -> int:
    """
    FNV-1a 64-bit hash function (matches C decision cache)

    This must produce identical hashes to the C implementation
    in phase1/src/cache/decision_cache.c for cache lookups to work.

    Args:
        data (str): String to hash

    Returns:
        int: 64-bit hash value
    """
    hash_value = FNV_64_OFFSET_BASIS

    for byte in data.encode('utf-8'):
        hash_value ^= byte
        hash_value = (hash_value * FNV_64_PRIME) & 0xFFFFFFFFFFFFFFFF

    return hash_value

# ============================================================================
# Command Parser
# ============================================================================

class CommandParser:
    """
    Parses AI responses into kernel commands

    Handles multiple response formats:
    1. JSON format (preferred)
    2. Plain text with keywords
    3. Natural language (requires mapping)
    """

    def __init__(self):
        self.logger = logging.getLogger('CommandParser')

        # Keyword to command mapping (order matters - longer/more specific first)
        self.keyword_map = {
            # Multi-word phrases first (more specific)
            'network status': 'NETWORK_STATUS',
            'system info': 'SYSTEM_INFO',
            'list processes': 'LIST_PROCESSES',
            'disk space': 'READ_DISK_STATS',
            # Single keywords second
            'cpu': 'READ_CPU_STATS',
            'memory': 'READ_MEMORY_STATS',
            'ram': 'READ_MEMORY_STATS',
            'disk': 'READ_DISK_STATS',
            'storage': 'READ_DISK_STATS',
            'process': 'LIST_PROCESSES',
            'help': 'SHOW_HELP',
            'network': 'NETWORK_STATUS',
            'status': 'SHOW_STATUS',
            'info': 'SYSTEM_INFO',
            'uptime': 'UPTIME',
            'temperature': 'TEMPERATURE',
            'temp': 'TEMPERATURE',
        }

    def parse(self, ai_response: str, query: str) -> Dict:
        """
        Parse AI response into command structure

        Args:
            ai_response (str): Raw AI response
            query (str): Original normalized query (fallback for keyword extraction)

        Returns:
            dict: Command structure
                {
                    'command': str,          # Command type
                    'parameters': dict,      # Command parameters
                    'trust_level': int,      # Safety trust level
                    'success': bool,         # Parsing success
                    'error': str             # Error message if failed
                }
        """
        # Try JSON format first
        command = self._parse_json(ai_response)
        if command['success']:
            return command

        # Try keyword extraction from query
        command = self._parse_keywords(query)
        if command['success']:
            return command

        # Fallback: return error
        return {
            'command': 'UNKNOWN',
            'parameters': {},
            'trust_level': TRUST_LEVELS['REQUIRE'],
            'success': False,
            'error': f"Could not parse AI response: {ai_response[:50]}"
        }

    def _parse_json(self, response: str) -> Dict:
        """Try to parse response as JSON"""
        try:
            data = json.loads(response)

            command_type = data.get('command', 'UNKNOWN')
            parameters = data.get('parameters', {})
            trust_level = data.get('trust_level', TRUST_LEVELS['REQUEST'])

            # Validate command type
            if command_type not in COMMAND_TYPES:
                return {'success': False, 'error': f"Unknown command: {command_type}"}

            return {
                'command': command_type,
                'parameters': parameters,
                'trust_level': trust_level,
                'success': True,
                'error': None
            }
        except json.JSONDecodeError:
            return {'success': False, 'error': 'Not JSON format'}

    def _parse_keywords(self, query: str) -> Dict:
        """Extract command from query keywords"""
        query_lower = query.lower()

        # Find first matching keyword
        for keyword, command in self.keyword_map.items():
            if keyword in query_lower:
                # Determine trust level based on command type
                trust_level = TRUST_LEVELS['AUTOMATIC']  # Most monitoring commands are safe

                return {
                    'command': command,
                    'parameters': {},
                    'trust_level': trust_level,
                    'success': True,
                    'error': None
                }

        return {'success': False, 'error': 'No keywords matched'}

# ============================================================================
# Query Processor (Main Pipeline)
# ============================================================================

class QueryProcessor:
    """
    End-to-end query processing pipeline

    Flow:
    1. Normalize query
    2. Hash normalized query
    3. Lookup in cache
    4. If cache miss, trigger AI inference
    5. Parse response into command
    6. Return command structure
    """

    def __init__(self, cache=None, ipc_client=None):
        """
        Initialize query processor

        Args:
            cache: Decision cache instance (optional, legacy Week 6)
            ipc_client: IPC client for seL4 communication (Week 8+)
        """
        self.normalizer = QueryNormalizer()
        self.parser = CommandParser()
        self.cache = cache
        self.ipc_client = ipc_client
        self.logger = logging.getLogger('QueryProcessor')

        # Statistics
        self.stats = {
            'total_queries': 0,
            'cache_hits': 0,
            'cache_misses': 0,
            'parse_successes': 0,
            'parse_failures': 0,
        }

    def process(self, user_query: str, ai_response: Optional[str] = None) -> Tuple[Dict, bool]:
        """
        Process a user query through the pipeline

        Args:
            user_query (str): Raw user input
            ai_response (str, optional): AI response if cache miss

        Returns:
            tuple: (command_dict, cache_hit)
                command_dict: Parsed command structure
                cache_hit: True if cache hit, False if AI inference used
        """
        self.stats['total_queries'] += 1

        # Step 1: Normalize query
        normalized = self.normalizer.normalize(user_query)
        self.logger.info(f"Normalized: '{user_query}' → '{normalized}'")

        # Step 2: Hash normalized query
        query_hash = fnv1a_hash_64(normalized)
        self.logger.info(f"Hash: 0x{query_hash:016x}")

        # Step 3: Check cache (if available via IPC or legacy cache)
        if self.cache or self.ipc_client:
            cached_command = self._cache_lookup(query_hash, normalized)
            if cached_command:
                self.stats['cache_hits'] += 1
                self.logger.info(f"Cache HIT for '{normalized}'")
                return cached_command, True

        # Step 4: Cache miss - need AI inference
        self.stats['cache_misses'] += 1
        self.logger.info(f"Cache MISS for '{normalized}'")

        # Step 5: Parse AI response (or use keywords as fallback)
        if ai_response:
            command = self.parser.parse(ai_response, normalized)
        else:
            # Fallback: try keyword extraction from query
            command = self.parser.parse("", normalized)

        # Track parsing stats
        if command['success']:
            self.stats['parse_successes'] += 1
        else:
            self.stats['parse_failures'] += 1

        # Step 6: Update cache (if parsing succeeded)
        if self.cache and command['success']:
            self._cache_update(normalized, query_hash, command)

        return command, False

    def _cache_lookup(self, query_hash: int, normalized_query: str = "") -> Optional[Dict]:
        """
        Lookup command in cache via IPC to seL4

        Week 8+: IPC call to C decision cache in seL4

        Args:
            query_hash (int): FNV-1a hash of normalized query
            normalized_query (str): Normalized query text (for IPC)

        Returns:
            dict or None: Cached command if found, None otherwise
        """
        # If no IPC client, cannot do cache lookup
        if not self.ipc_client:
            self.logger.debug("No IPC client - cache lookup disabled")
            return None

        try:
            # Import message types (avoiding circular import)
            from ipc_client import MSG_QUERY

            # Send query to seL4 via IPC
            if not self.ipc_client.send_message(MSG_QUERY, normalized_query):
                self.logger.warning("Failed to send query to seL4")
                return None

            # Receive response from seL4 (with timeout)
            response_msg = self.ipc_client.receive_message(timeout_ms=1000)
            if not response_msg:
                self.logger.warning("No response from seL4 (timeout)")
                return None

            # Parse response payload (format: "ACTION:xxx|TRUST:x|HIT:x")
            payload = response_msg.payload[:response_msg.payload_size].decode('utf-8', errors='ignore')
            self.logger.debug(f"Received from seL4: {payload}")

            # Parse response format
            parts = payload.split('|')
            response_dict = {}
            for part in parts:
                if ':' in part:
                    key, value = part.split(':', 1)
                    response_dict[key.strip()] = value.strip()

            # Check cache hit
            cache_hit = response_dict.get('HIT', '0') == '1'

            if not cache_hit:
                self.logger.debug("Cache miss in seL4")
                return None

            # Cache hit - parse command
            action = response_dict.get('ACTION', 'UNKNOWN')
            trust_level = int(response_dict.get('TRUST', str(TRUST_LEVELS['REQUEST'])))

            # Map action to command type
            command_type = action if action in COMMAND_TYPES else 'UNKNOWN'

            return {
                'command': command_type,
                'parameters': {},
                'trust_level': trust_level,
                'success': True,
                'error': None
            }

        except Exception as e:
            self.logger.error(f"Cache lookup error: {e}")
            return None

    def _cache_update(self, normalized_query: str, query_hash: int, command: Dict):
        """
        Update cache with new query→command mapping

        Week 8+: IPC call to C cache to insert entry (optional)
        Note: seL4 decision cache is pre-loaded with 200 patterns,
        dynamic updates are not critical for Phase 1.

        Args:
            normalized_query (str): Normalized query text
            query_hash (int): FNV-1a hash
            command (dict): Parsed command structure
        """
        # Cache updates not implemented for Phase 1
        # seL4 decision cache is pre-loaded and read-only
        # Dynamic cache updates deferred to Phase 2
        self.logger.debug(f"Cache update not implemented (query: '{normalized_query}')")
        pass

    def get_statistics(self) -> Dict:
        """
        Get query processing statistics

        Returns:
            dict: Statistics including cache hit rate
        """
        total = self.stats['total_queries']
        if total == 0:
            hit_rate = 0.0
        else:
            hit_rate = (self.stats['cache_hits'] / total) * 100

        return {
            **self.stats,
            'cache_hit_rate': hit_rate
        }

    def print_statistics(self):
        """Print formatted statistics"""
        stats = self.get_statistics()

        print("=" * 70)
        print("[QUERY PROCESSOR STATISTICS]")
        print("=" * 70)
        print(f"  Total queries:     {stats['total_queries']}")
        print(f"  Cache hits:        {stats['cache_hits']}")
        print(f"  Cache misses:      {stats['cache_misses']}")
        print(f"  Cache hit rate:    {stats['cache_hit_rate']:.1f}%")
        print(f"  Parse successes:   {stats['parse_successes']}")
        print(f"  Parse failures:    {stats['parse_failures']}")
        print("=" * 70)

# ============================================================================
# Utility Functions
# ============================================================================

def validate_command(command: Dict) -> bool:
    """
    Validate command structure

    Args:
        command (dict): Command to validate

    Returns:
        bool: True if valid, False otherwise
    """
    required_fields = ['command', 'parameters', 'trust_level', 'success']

    for field in required_fields:
        if field not in command:
            return False

    if command['command'] not in COMMAND_TYPES and command['command'] != 'UNKNOWN':
        return False

    if not isinstance(command['parameters'], dict):
        return False

    if command['trust_level'] not in TRUST_LEVELS.values():
        return False

    return True

# ============================================================================
# Main (for testing)
# ============================================================================

if __name__ == "__main__":
    # Configure logging
    logging.basicConfig(level=logging.INFO, format='%(message)s')

    # Create processor
    processor = QueryProcessor()

    print("=" * 70)
    print("JARVIS Query Processor - Week 6 Test")
    print("=" * 70)
    print()

    # Test queries
    test_queries = [
        "  SHOW   CPU  ",
        "What's the memory usage?",
        "   help   ",
        "check disk space",
        "network status",
        "system info",
    ]

    for query in test_queries:
        print(f"Query: '{query}'")

        # Process without AI response (keyword fallback)
        command, cache_hit = processor.process(query)

        print(f"  Normalized: '{processor.normalizer.normalize(query)}'")
        print(f"  Cache hit: {cache_hit}")
        print(f"  Command: {command['command']}")
        print(f"  Success: {command['success']}")
        if not command['success']:
            print(f"  Error: {command['error']}")
        print()

    # Print statistics
    processor.print_statistics()
