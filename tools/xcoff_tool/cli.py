#!/usr/bin/env python3
"""
Command-line interface for XCOFF analysis tool.
"""

import argparse
import sys
import os
from pathlib import Path
from typing import List, Optional

from . import __version__
from .core import SnapshotManager, XCOFFValidator, ComparisonEngine
from .storage import Storage
from .output import TextFormatter, JSONFormatter


def create_parser() -> argparse.ArgumentParser:
    """Create argument parser."""
    parser = argparse.ArgumentParser(
        prog="xcoff",
        description="XCOFF analysis and comparison tool for AIX/IBM i PASE",
    )
    parser.add_argument(
        "--version", action="version", version=f"%(prog)s {__version__}"
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0,
        help="Increase verbosity (can stack: -vv)"
    )
    parser.add_argument(
        "-q", "--quiet", action="store_true",
        help="Suppress non-essential output"
    )
    parser.add_argument(
        "-f", "--format", choices=["text", "json"], default="text",
        help="Output format (default: text)"
    )
    parser.add_argument(
        "-o", "--output", type=str,
        help="Write output to file"
    )
    parser.add_argument(
        "--db-path", type=str,
        help="Override database path (default: ~/.xcoffscandb)"
    )

    subparsers = parser.add_subparsers(dest="command", help="Commands")

    # snapshot command
    snap_parser = subparsers.add_parser(
        "snapshot", help="Capture analysis snapshot of an XCOFF file"
    )
    snap_parser.add_argument("files", nargs="+", help="XCOFF file(s) to analyze")
    snap_parser.add_argument(
        "-a", "--analyzers", type=str,
        help="Comma-separated analyzer names (default: all)"
    )
    snap_parser.add_argument(
        "-x", "--exclude", type=str,
        help="Exclude these analyzers"
    )
    snap_parser.add_argument(
        "--store", type=str, metavar="NAME",
        help="Store snapshot with name (e.g., aix73, ibmi75, baseline)"
    )
    snap_parser.add_argument(
        "--timeout", type=int, default=300,
        help="Per-analyzer timeout in seconds (default: 300)"
    )

    # compare command
    cmp_parser = subparsers.add_parser(
        "compare", help="Compare two XCOFF files directly"
    )
    cmp_parser.add_argument("file1", help="First XCOFF file")
    cmp_parser.add_argument("file2", nargs="?", help="Second XCOFF file")
    cmp_parser.add_argument(
        "--against", type=str, metavar="NAME",
        help="Compare against named stored snapshot"
    )
    cmp_parser.add_argument(
        "-a", "--analyzers", type=str,
        help="Comma-separated analyzer names"
    )
    cmp_parser.add_argument(
        "--summary-only", action="store_true",
        help="Show only summary"
    )

    # diff command
    diff_parser = subparsers.add_parser(
        "diff", help="Compare two named snapshots"
    )
    diff_parser.add_argument("name1", help="First snapshot name")
    diff_parser.add_argument("name2", help="Second snapshot name")
    diff_parser.add_argument("file", nargs="?", help="Specific file to compare")
    diff_parser.add_argument(
        "--summary-only", action="store_true",
        help="Show only summary"
    )

    # info command
    info_parser = subparsers.add_parser(
        "info", help="Quick summary of XCOFF file"
    )
    info_parser.add_argument("file", help="XCOFF file")

    # validate command
    val_parser = subparsers.add_parser(
        "validate", help="Check if file is valid XCOFF"
    )
    val_parser.add_argument("file", help="File to validate")

    # list command
    list_parser = subparsers.add_parser(
        "list", help="List available analyzers or stored snapshots"
    )
    list_parser.add_argument(
        "--stored", nargs="?", const="", metavar="NAME",
        help="List stored snapshots, or files in a specific snapshot"
    )
    list_parser.add_argument(
        "--check", action="store_true",
        help="Check if required commands are available"
    )

    return parser


def get_db_path(args) -> Path:
    """Get database path from args or environment."""
    if args.db_path:
        return Path(args.db_path)
    if "XCOFF_DB_PATH" in os.environ:
        return Path(os.environ["XCOFF_DB_PATH"])
    return Path.home() / ".xcoffscandb"


def get_formatter(args):
    """Get output formatter based on args."""
    if args.format == "json":
        return JSONFormatter()
    return TextFormatter(verbose=args.verbose)


def cmd_snapshot(args) -> int:
    """Handle snapshot command."""
    formatter = get_formatter(args)
    manager = SnapshotManager(timeout=args.timeout, verbose=args.verbose)
    storage = Storage(get_db_path(args)) if args.store else None

    # Parse analyzer selection
    analyzers = None
    if args.analyzers:
        analyzers = [a.strip() for a in args.analyzers.split(",")]
    exclude = None
    if args.exclude:
        exclude = [a.strip() for a in args.exclude.split(",")]

    results = []
    for filepath in args.files:
        path = Path(filepath)
        if not path.exists():
            print(f"Error: File not found: {filepath}", file=sys.stderr)
            continue

        # Validate first
        validator = XCOFFValidator()
        validation = validator.validate(str(path))
        if not validation.valid:
            print(f"Error: {filepath}: {validation.error}", file=sys.stderr)
            continue

        # Capture snapshot
        snapshot = manager.capture(str(path), analyzers=analyzers, exclude=exclude)
        results.append(snapshot)

        # Store if requested
        if storage and args.store:
            storage.store(args.store, snapshot)
            if not args.quiet:
                print(f"Stored: {filepath} -> {args.store}", file=sys.stderr)

    # Output results
    output = formatter.format_snapshots(results)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output)

    return 0 if results else 1


def cmd_compare(args) -> int:
    """Handle compare command."""
    formatter = get_formatter(args)
    manager = SnapshotManager(verbose=args.verbose)
    engine = ComparisonEngine()

    # Parse analyzer selection
    analyzers = None
    if args.analyzers:
        analyzers = [a.strip() for a in args.analyzers.split(",")]

    # Get first file snapshot
    path1 = Path(args.file1)
    if not path1.exists():
        print(f"Error: File not found: {args.file1}", file=sys.stderr)
        return 1

    snapshot1 = manager.capture(str(path1), analyzers=analyzers)

    # Get second snapshot
    if args.against:
        # Compare against stored snapshot
        storage = Storage(get_db_path(args))
        snapshot2 = storage.load(args.against, str(path1))
        if not snapshot2:
            print(f"Error: No stored snapshot '{args.against}' for {args.file1}", file=sys.stderr)
            return 1
    elif args.file2:
        path2 = Path(args.file2)
        if not path2.exists():
            print(f"Error: File not found: {args.file2}", file=sys.stderr)
            return 1
        snapshot2 = manager.capture(str(path2), analyzers=analyzers)
    else:
        print("Error: Need --against NAME or second file", file=sys.stderr)
        return 1

    # Compare
    comparison = engine.compare(snapshot1, snapshot2)

    # Output
    output = formatter.format_comparison(comparison, summary_only=args.summary_only)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output)

    return 0 if not comparison.has_differences else 1


def cmd_diff(args) -> int:
    """Handle diff command."""
    formatter = get_formatter(args)
    storage = Storage(get_db_path(args))
    engine = ComparisonEngine()

    if args.file:
        # Compare specific file between two snapshots
        snap1 = storage.load(args.name1, args.file)
        snap2 = storage.load(args.name2, args.file)

        if not snap1:
            print(f"Error: No snapshot '{args.name1}' for {args.file}", file=sys.stderr)
            return 1
        if not snap2:
            print(f"Error: No snapshot '{args.name2}' for {args.file}", file=sys.stderr)
            return 1

        comparison = engine.compare(snap1, snap2)
        output = formatter.format_comparison(comparison, summary_only=args.summary_only)
    else:
        # Compare all files in both snapshots
        files1 = set(storage.list_files(args.name1))
        files2 = set(storage.list_files(args.name2))

        all_files = files1 | files2
        comparisons = []

        for filepath in sorted(all_files):
            snap1 = storage.load(args.name1, filepath)
            snap2 = storage.load(args.name2, filepath)

            if snap1 and snap2:
                comp = engine.compare(snap1, snap2)
                if comp.has_differences:
                    comparisons.append((filepath, comp))
            elif snap1:
                comparisons.append((filepath, "removed"))
            else:
                comparisons.append((filepath, "added"))

        output = formatter.format_diff_summary(args.name1, args.name2, comparisons)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output)

    return 0


def cmd_info(args) -> int:
    """Handle info command."""
    formatter = get_formatter(args)
    validator = XCOFFValidator()

    path = Path(args.file)
    if not path.exists():
        print(f"Error: File not found: {args.file}", file=sys.stderr)
        return 1

    validation = validator.validate(str(path))
    output = formatter.format_info(validation)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output)

    return 0 if validation.valid else 1


def cmd_validate(args) -> int:
    """Handle validate command."""
    validator = XCOFFValidator()

    path = Path(args.file)
    if not path.exists():
        print(f"Error: File not found: {args.file}", file=sys.stderr)
        return 1

    validation = validator.validate(str(path))

    if validation.valid:
        print(f"{args.file}: valid {validation.file_type}")
        return 0
    else:
        print(f"{args.file}: invalid - {validation.error}")
        return 1


def cmd_list(args) -> int:
    """Handle list command."""
    formatter = get_formatter(args)

    if args.stored is not None:
        # List stored snapshots
        storage = Storage(get_db_path(args))

        if args.stored == "":
            # List all snapshot names
            snapshots = storage.list_snapshots()
            if not snapshots:
                print("No stored snapshots")
                return 0

            print("Stored snapshots:")
            for name, info in snapshots.items():
                print(f"  {name}: {info['file_count']} files ({info['created']})")
        else:
            # List files in specific snapshot
            files = storage.list_files(args.stored)
            if not files:
                print(f"No files in snapshot '{args.stored}'")
                return 1

            print(f"Files in '{args.stored}':")
            for f in sorted(files):
                print(f"  {f}")
    else:
        # List analyzers
        from .analyzers import get_all_analyzers

        analyzers = get_all_analyzers()
        print("Available analyzers:")
        for name, analyzer in analyzers.items():
            status = ""
            if args.check:
                missing = analyzer.check_requirements()
                status = " [OK]" if not missing else f" [MISSING: {', '.join(missing)}]"
            print(f"  {name}: {analyzer.description}{status}")

    return 0


def main() -> int:
    """Main entry point."""
    parser = create_parser()
    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 0

    commands = {
        "snapshot": cmd_snapshot,
        "compare": cmd_compare,
        "diff": cmd_diff,
        "info": cmd_info,
        "validate": cmd_validate,
        "list": cmd_list,
    }

    handler = commands.get(args.command)
    if handler:
        try:
            return handler(args)
        except KeyboardInterrupt:
            print("\nInterrupted", file=sys.stderr)
            return 130
        except Exception as e:
            if args.verbose:
                raise
            print(f"Error: {e}", file=sys.stderr)
            return 1
    else:
        parser.print_help()
        return 1


if __name__ == "__main__":
    sys.exit(main())
