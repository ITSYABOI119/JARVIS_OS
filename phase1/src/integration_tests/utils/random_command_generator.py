#!/usr/bin/env python3
"""
Random Command Generator - Generate weighted random commands for stress testing

Generates realistic command sequences for stability testing:
- 80% safe commands (help, status, ls, ps, etc.)
- 15% SHIELD-validated commands (mkdir, rm, kill)
- 5% SHIELD-blocked commands (shutdown, reboot)

Usage:
    generator = RandomCommandGenerator()
    cmd = generator.generate()
    print(f"Command: {cmd}")

    # Generate sequence
    commands = generator.generate_sequence(count=100)
"""

import random
import os
import tempfile
from typing import List, Dict, Tuple
from enum import Enum


class CommandCategory(Enum):
    """Command safety categories"""
    SAFE = "safe"                    # Execute without restrictions
    SHIELD_VALIDATED = "validated"  # Require SHIELD validation
    SHIELD_BLOCKED = "blocked"       # Should be blocked by SHIELD


class RandomCommandGenerator:
    """Generate random valid commands for stress testing"""

    def __init__(self, seed: int = None):
        """
        Initialize command generator

        Args:
            seed: Random seed for reproducibility (optional)
        """
        if seed is not None:
            random.seed(seed)

        # Command templates by category
        self.commands = {
            CommandCategory.SAFE: [
                # Built-in commands
                'help',
                'status',
                'cache',
                'agent',
                'shield',
                'clear',

                # File operations (safe - read only)
                'ls',
                'ls /',
                'ls /tmp',
                'ls {temp_dir}',
                'cat /etc/hosts',  # Safe read-only file

                # Process management (safe - read only)
                'ps',
                'top',

                # System info (safe - read only)
                'battery',
                'uptime',

                # Network (safe - no modifications)
                'ifconfig',
                'ping 127.0.0.1',
                'ping localhost',

                # AI queries (safe)
                'query what is the weather',
                'query list processes',
                'query show files',
                'query system status',
            ],

            CommandCategory.SHIELD_VALIDATED: [
                # File operations (require SHIELD validation)
                'mkdir {temp_dir}/test_{id}',
                'mkdir /tmp/jarvis_test_{id}',
                'rm {temp_file}',
                'rm {temp_dir}/test_{id}',

                # Process operations (require SHIELD validation)
                'kill {safe_pid}',  # Use own PID (safe)
            ],

            CommandCategory.SHIELD_BLOCKED: [
                # Critical system operations (should be blocked)
                'shutdown',
                'reboot',
            ]
        }

        # Weight distribution (total must sum to 1.0)
        self.weights = {
            CommandCategory.SAFE: 0.80,           # 80% safe
            CommandCategory.SHIELD_VALIDATED: 0.15, # 15% validated
            CommandCategory.SHIELD_BLOCKED: 0.05   # 5% blocked
        }

        # Track created resources for cleanup
        self.temp_files: List[str] = []
        self.temp_dirs: List[str] = []

        # Get temp directory
        self.temp_base_dir = tempfile.gettempdir()

    def generate(self) -> Tuple[str, CommandCategory]:
        """
        Generate single random command

        Returns:
            Tuple of (command_string, category)
        """
        # Select category based on weights
        category = random.choices(
            list(self.weights.keys()),
            weights=list(self.weights.values())
        )[0]

        # Select random command template
        cmd_template = random.choice(self.commands[category])

        # Fill in placeholders
        cmd = self._fill_placeholders(cmd_template, category)

        return cmd, category

    def _fill_placeholders(self, template: str, category: CommandCategory) -> str:
        """
        Fill in placeholders in command template

        Placeholders:
        - {id}: Random 4-digit number
        - {safe_pid}: Current process PID (safe to kill)
        - {temp_file}: Path to temp file
        - {temp_dir}: Path to temp directory
        """
        cmd = template

        # Replace {id} with random number
        if '{id}' in cmd:
            random_id = random.randint(1000, 9999)
            cmd = cmd.replace('{id}', str(random_id))

        # Replace {safe_pid} with own PID
        if '{safe_pid}' in cmd:
            cmd = cmd.replace('{safe_pid}', str(os.getpid()))

        # Replace {temp_file}
        if '{temp_file}' in cmd:
            # Create temp file if needed
            if not self.temp_files:
                self._create_temp_file()
            temp_file = random.choice(self.temp_files)
            cmd = cmd.replace('{temp_file}', temp_file)

        # Replace {temp_dir}
        if '{temp_dir}' in cmd:
            cmd = cmd.replace('{temp_dir}', self.temp_base_dir)

        return cmd

    def _create_temp_file(self):
        """Create temp file for testing"""
        fd, path = tempfile.mkstemp(prefix='jarvis_test_', suffix='.txt')
        os.write(fd, b'JARVIS test file\n')
        os.close(fd)
        self.temp_files.append(path)

    def _create_temp_dir(self):
        """Create temp directory for testing"""
        path = tempfile.mkdtemp(prefix='jarvis_test_')
        self.temp_dirs.append(path)

    def generate_sequence(self, count: int) -> List[Tuple[str, CommandCategory]]:
        """
        Generate sequence of random commands

        Args:
            count: Number of commands to generate

        Returns:
            List of (command, category) tuples
        """
        return [self.generate() for _ in range(count)]

    def get_category_distribution(self, sequence: List[Tuple[str, CommandCategory]]) -> Dict[CommandCategory, int]:
        """
        Get distribution of categories in sequence

        Args:
            sequence: List of (command, category) tuples

        Returns:
            Dictionary of category -> count
        """
        distribution = {
            CommandCategory.SAFE: 0,
            CommandCategory.SHIELD_VALIDATED: 0,
            CommandCategory.SHIELD_BLOCKED: 0
        }

        for _, category in sequence:
            distribution[category] += 1

        return distribution

    def validate_distribution(self, sequence: List[Tuple[str, CommandCategory]], tolerance: float = 0.05) -> bool:
        """
        Validate that sequence matches expected distribution

        Args:
            sequence: List of (command, category) tuples
            tolerance: Allowed deviation from target weights (default: 5%)

        Returns:
            True if distribution is within tolerance
        """
        distribution = self.get_category_distribution(sequence)
        total = len(sequence)

        for category, expected_weight in self.weights.items():
            actual_weight = distribution[category] / total
            deviation = abs(actual_weight - expected_weight)

            if deviation > tolerance:
                return False

        return True

    def cleanup(self):
        """Clean up temporary files and directories"""
        # Remove temp files
        for temp_file in self.temp_files:
            try:
                if os.path.exists(temp_file):
                    os.remove(temp_file)
            except Exception:
                pass  # Ignore cleanup errors

        # Remove temp directories
        for temp_dir in self.temp_dirs:
            try:
                if os.path.exists(temp_dir):
                    import shutil
                    shutil.rmtree(temp_dir)
            except Exception:
                pass  # Ignore cleanup errors

        self.temp_files.clear()
        self.temp_dirs.clear()


# Example usage
if __name__ == "__main__":
    print("Random Command Generator Example")
    print("=" * 70)

    # Create generator
    generator = RandomCommandGenerator(seed=42)  # Use seed for reproducibility

    # Generate 20 random commands
    print("\nGenerating 20 random commands:\n")
    for i in range(20):
        cmd, category = generator.generate()
        category_str = {
            CommandCategory.SAFE: "[SAFE]",
            CommandCategory.SHIELD_VALIDATED: "[VALIDATED]",
            CommandCategory.SHIELD_BLOCKED: "[BLOCKED]"
        }[category]

        print(f"{i+1:2d}. {category_str:12s} {cmd}")

    # Generate larger sequence and check distribution
    print("\n" + "=" * 70)
    print("Testing distribution with 1000 commands:\n")

    sequence = generator.generate_sequence(1000)
    distribution = generator.get_category_distribution(sequence)

    print("Expected distribution:")
    for category, weight in generator.weights.items():
        print(f"  {category.value:12s}: {weight*100:5.1f}%")

    print("\nActual distribution:")
    for category, count in distribution.items():
        percentage = (count / len(sequence)) * 100
        print(f"  {category.value:12s}: {percentage:5.1f}% ({count} commands)")

    # Validate distribution
    is_valid = generator.validate_distribution(sequence, tolerance=0.05)
    print(f"\nDistribution valid (5% tolerance): {is_valid}")

    # Clean up
    print("\nCleaning up temporary files...")
    generator.cleanup()
    print("Cleanup complete")
