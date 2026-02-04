#!/usr/bin/env python3
"""
Entry point for xcoff tool.

Usage: python -m xcoff_tool <command> [options]
"""

from .cli import main

if __name__ == "__main__":
    main()
