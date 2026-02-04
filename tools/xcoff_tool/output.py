#!/usr/bin/env python3
"""
Output formatters: text and JSON.
"""

import json
from typing import List, Dict, Any, Optional

from .core import Snapshot, ComparisonResult, ValidationResult


class TextFormatter:
    """Plain text output formatter."""

    def __init__(self, verbose: int = 0):
        self.verbose = verbose

    def format_snapshots(self, snapshots: List[Snapshot]) -> str:
        """Format snapshot results as text."""
        lines = []

        for snap in snapshots:
            lines.append("=" * 60)
            lines.append(f"File: {snap.filepath}")
            lines.append(f"Type: {snap.file_type}")
            lines.append(f"Size: {snap.file_size} bytes")
            lines.append(f"Modified: {snap.file_mtime}")
            lines.append(f"Analyzed: {snap.timestamp}")
            if snap.name:
                lines.append(f"Stored as: {snap.name}")
            lines.append("")

            for name, result in snap.results.items():
                lines.append(f"[{name}]")
                if result.success:
                    lines.extend(self._format_analyzer_data(name, result.data))
                else:
                    lines.append(f"  ERROR: {result.error}")
                lines.append("")

        return "\n".join(lines)

    def _format_analyzer_data(self, name: str, data: Dict) -> List[str]:
        """Format analyzer-specific data."""
        lines = []

        if name == "what":
            strings = data.get("strings", [])
            lines.append(f"  Identification strings: {len(strings)}")
            for s in strings:
                lines.append(f"    {s}")

        elif name == "dump-h":
            sections = data.get("sections", [])
            lines.append(f"  Sections: {len(sections)}")
            for sec in sections:
                sec_name = sec.get("name", "?")
                sec_size = sec.get("size", "?")
                lines.append(f"    {sec_name}: size={sec_size}")

        elif name == "dump-T":
            imports = data.get("imports", [])
            exports = data.get("exports", [])
            lines.append(f"  Imports: {len(imports)}")
            lines.append(f"  Exports: {len(exports)}")

            if self.verbose:
                if imports:
                    lines.append("  Import symbols:")
                    for sym in imports[:20]:  # Limit display
                        lines.append(f"    {sym.get('name', '?')}")
                    if len(imports) > 20:
                        lines.append(f"    ... and {len(imports) - 20} more")

                if exports:
                    lines.append("  Export symbols:")
                    for sym in exports[:20]:
                        addr = sym.get("address", "")
                        name = sym.get("name", "?")
                        lines.append(f"    {addr}  {name}")
                    if len(exports) > 20:
                        lines.append(f"    ... and {len(exports) - 20} more")
        else:
            # Generic formatting
            for key, value in data.items():
                if key == "raw":
                    continue  # Skip raw output
                if isinstance(value, list):
                    lines.append(f"  {key}: {len(value)} items")
                else:
                    lines.append(f"  {key}: {value}")

        return lines

    def format_comparison(
        self, comparison: ComparisonResult, summary_only: bool = False
    ) -> str:
        """Format comparison result as text."""
        lines = []

        lines.append("=" * 60)
        lines.append("Comparison Result")
        lines.append("=" * 60)
        lines.append(f"File 1: {comparison.filepath1}")
        lines.append(f"File 2: {comparison.filepath2}")
        lines.append("")

        if comparison.has_differences:
            lines.append("Status: DIFFERENT")
            lines.append(f"Summary: {comparison.summary}")
        else:
            lines.append("Status: IDENTICAL")
            return "\n".join(lines)

        if summary_only:
            return "\n".join(lines)

        lines.append("")
        lines.append("Details:")
        lines.append("-" * 40)

        for name, diff in comparison.analyzer_diffs.items():
            if name == "_metadata":
                lines.append("[metadata]")
                for key, change in diff.items():
                    lines.append(f"  {key}: {change.get('old')} -> {change.get('new')}")
                continue

            lines.append(f"[{name}]")

            if "status" in diff:
                lines.append(f"  {diff['status']}")
            elif "added" in diff and "removed" in diff:
                added = diff.get("added", [])
                removed = diff.get("removed", [])
                if added:
                    lines.append(f"  Added ({len(added)}):")
                    for item in added[:10]:
                        lines.append(f"    + {item}")
                    if len(added) > 10:
                        lines.append(f"    ... and {len(added) - 10} more")
                if removed:
                    lines.append(f"  Removed ({len(removed)}):")
                    for item in removed[:10]:
                        lines.append(f"    - {item}")
                    if len(removed) > 10:
                        lines.append(f"    ... and {len(removed) - 10} more")
            elif "imports" in diff or "exports" in diff:
                # Loader section diff
                if "imports" in diff:
                    imp = diff["imports"]
                    added = imp.get("added", [])
                    removed = imp.get("removed", [])
                    if added or removed:
                        lines.append(f"  Imports: +{len(added)} -{len(removed)}")
                        for s in added[:5]:
                            lines.append(f"    + {s}")
                        for s in removed[:5]:
                            lines.append(f"    - {s}")
                if "exports" in diff:
                    exp = diff["exports"]
                    added = exp.get("added", [])
                    removed = exp.get("removed", [])
                    if added or removed:
                        lines.append(f"  Exports: +{len(added)} -{len(removed)}")
                        for s in added[:5]:
                            lines.append(f"    + {s}")
                        for s in removed[:5]:
                            lines.append(f"    - {s}")
            else:
                # Section changes
                for sec_name, sec_diff in diff.items():
                    if isinstance(sec_diff, dict):
                        status = sec_diff.get("status", "changed")
                        lines.append(f"  {sec_name}: {status}")

            lines.append("")

        return "\n".join(lines)

    def format_diff_summary(
        self,
        name1: str,
        name2: str,
        comparisons: List[tuple],
    ) -> str:
        """Format diff summary between two named snapshots."""
        lines = []

        lines.append("=" * 60)
        lines.append(f"Diff: {name1} vs {name2}")
        lines.append("=" * 60)

        changed = []
        added = []
        removed = []

        for item in comparisons:
            filepath, result = item
            if result == "added":
                added.append(filepath)
            elif result == "removed":
                removed.append(filepath)
            else:
                changed.append((filepath, result))

        lines.append(f"Changed: {len(changed)}")
        lines.append(f"Added: {len(added)}")
        lines.append(f"Removed: {len(removed)}")
        lines.append("")

        if changed:
            lines.append("Changed files:")
            for filepath, comp in changed:
                lines.append(f"  {filepath}")
                lines.append(f"    {comp.summary}")

        if added:
            lines.append("")
            lines.append("Added files:")
            for filepath in added:
                lines.append(f"  + {filepath}")

        if removed:
            lines.append("")
            lines.append("Removed files:")
            for filepath in removed:
                lines.append(f"  - {filepath}")

        return "\n".join(lines)

    def format_info(self, validation: ValidationResult) -> str:
        """Format validation info."""
        lines = []

        if validation.valid:
            lines.append(f"Type: {validation.file_type}")
            lines.append("")
            lines.append("Details:")
            for key, value in validation.details.items():
                lines.append(f"  {key}: {value}")
        else:
            lines.append(f"Invalid: {validation.error}")
            if validation.details:
                lines.append("")
                lines.append("Details:")
                for key, value in validation.details.items():
                    lines.append(f"  {key}: {value}")

        return "\n".join(lines)


class JSONFormatter:
    """JSON output formatter."""

    def __init__(self, indent: int = 2):
        self.indent = indent

    def format_snapshots(self, snapshots: List[Snapshot]) -> str:
        """Format snapshots as JSON."""
        data = [s.to_dict() for s in snapshots]
        if len(data) == 1:
            data = data[0]
        return json.dumps(data, indent=self.indent)

    def format_comparison(
        self, comparison: ComparisonResult, summary_only: bool = False
    ) -> str:
        """Format comparison as JSON."""
        data = {
            "filepath1": comparison.filepath1,
            "filepath2": comparison.filepath2,
            "has_differences": comparison.has_differences,
            "summary": comparison.summary,
        }
        if not summary_only:
            data["diffs"] = comparison.analyzer_diffs
        return json.dumps(data, indent=self.indent)

    def format_diff_summary(
        self,
        name1: str,
        name2: str,
        comparisons: List[tuple],
    ) -> str:
        """Format diff summary as JSON."""
        changed = []
        added = []
        removed = []

        for filepath, result in comparisons:
            if result == "added":
                added.append(filepath)
            elif result == "removed":
                removed.append(filepath)
            else:
                changed.append({
                    "filepath": filepath,
                    "summary": result.summary,
                })

        data = {
            "snapshot1": name1,
            "snapshot2": name2,
            "changed": changed,
            "added": added,
            "removed": removed,
        }
        return json.dumps(data, indent=self.indent)

    def format_info(self, validation: ValidationResult) -> str:
        """Format validation info as JSON."""
        data = {
            "valid": validation.valid,
            "file_type": validation.file_type,
            "error": validation.error,
            "details": validation.details,
        }
        return json.dumps(data, indent=self.indent)
