#!/usr/bin/env python3
"""
Core functionality: snapshot capture, validation, comparison.
"""

import os
import struct
import subprocess
import hashlib
from pathlib import Path
from datetime import datetime
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any


@dataclass
class ValidationResult:
    """Result of XCOFF validation."""
    valid: bool
    file_type: Optional[str] = None  # xcoff32, xcoff64, archive
    error: Optional[str] = None
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class AnalysisResult:
    """Result from a single analyzer."""
    analyzer_name: str
    success: bool
    data: Dict[str, Any] = field(default_factory=dict)
    error: Optional[str] = None
    truncated: bool = False


@dataclass
class Snapshot:
    """Complete snapshot of an XCOFF file."""
    filepath: str
    timestamp: str
    file_size: int
    file_mtime: str
    file_type: str
    name: Optional[str] = None  # Storage name if stored
    results: Dict[str, AnalysisResult] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "version": "1.0",
            "name": self.name,
            "filepath": self.filepath,
            "timestamp": self.timestamp,
            "file_size": self.file_size,
            "file_mtime": self.file_mtime,
            "file_type": self.file_type,
            "results": {
                name: {
                    "success": r.success,
                    "data": r.data,
                    "error": r.error,
                    "truncated": r.truncated,
                }
                for name, r in self.results.items()
            },
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Snapshot":
        """Create from dictionary."""
        results = {}
        for name, r in data.get("results", {}).items():
            results[name] = AnalysisResult(
                analyzer_name=name,
                success=r.get("success", False),
                data=r.get("data", {}),
                error=r.get("error"),
                truncated=r.get("truncated", False),
            )

        return cls(
            filepath=data["filepath"],
            timestamp=data["timestamp"],
            file_size=data["file_size"],
            file_mtime=data["file_mtime"],
            file_type=data.get("file_type", "unknown"),
            name=data.get("name"),
            results=results,
        )


@dataclass
class ComparisonResult:
    """Result of comparing two snapshots."""
    filepath1: str
    filepath2: str
    has_differences: bool
    summary: str
    analyzer_diffs: Dict[str, Dict[str, Any]] = field(default_factory=dict)


class XCOFFValidator:
    """Validate XCOFF files."""

    MAGIC_32 = 0x01DF
    MAGIC_64 = 0x01F7
    AR_MAGIC = b"!<arch>\n"

    def validate(self, filepath: str) -> ValidationResult:
        """Validate file is a valid XCOFF object."""
        path = Path(filepath)

        if not path.exists():
            return ValidationResult(
                valid=False,
                error=f"File not found: {filepath}",
            )

        if not os.access(filepath, os.R_OK):
            return ValidationResult(
                valid=False,
                error=f"File not readable: {filepath}",
            )

        file_size = path.stat().st_size
        if file_size < 20:
            return ValidationResult(
                valid=False,
                error=f"File too small: {file_size} bytes",
                details={"file_size": file_size},
            )

        try:
            with open(filepath, "rb") as f:
                header = f.read(20)

                # Check for archive
                if header[:8] == self.AR_MAGIC:
                    return ValidationResult(
                        valid=True,
                        file_type="archive",
                        details={"file_size": file_size},
                    )

                # Check XCOFF magic (big-endian)
                magic = struct.unpack(">H", header[:2])[0]

                if magic == self.MAGIC_32:
                    return self._validate_xcoff(header, file_size, "xcoff32")
                elif magic == self.MAGIC_64:
                    return self._validate_xcoff(header, file_size, "xcoff64")
                else:
                    return ValidationResult(
                        valid=False,
                        error=f"Invalid magic: 0x{magic:04X}",
                        details={"magic": magic, "file_size": file_size},
                    )

        except IOError as e:
            return ValidationResult(
                valid=False,
                error=f"I/O error: {e}",
            )

    def _validate_xcoff(
        self, header: bytes, file_size: int, file_type: str
    ) -> ValidationResult:
        """Validate XCOFF header structure."""
        magic, nscns, timdat, symptr, nsyms, opthdr, flags = struct.unpack(
            ">HHIIIHH", header
        )

        details = {
            "magic": f"0x{magic:04X}",
            "sections": nscns,
            "timestamp": timdat,
            "symbol_table_offset": symptr,
            "symbol_count": nsyms,
            "optional_header_size": opthdr,
            "flags": f"0x{flags:04X}",
            "file_size": file_size,
        }

        errors = []
        if nscns > 1000:
            errors.append(f"Suspicious section count: {nscns}")
        if symptr > 0 and symptr > file_size:
            errors.append(f"Symbol table offset beyond file size")
        if opthdr > 1024:
            errors.append(f"Suspicious optional header size: {opthdr}")

        if errors:
            return ValidationResult(
                valid=False,
                file_type=file_type,
                error="; ".join(errors),
                details=details,
            )

        return ValidationResult(
            valid=True,
            file_type=file_type,
            details=details,
        )


class SnapshotManager:
    """Manage snapshot capture."""

    def __init__(self, timeout: int = 300, verbose: int = 0):
        self.timeout = timeout
        self.verbose = verbose

    def capture(
        self,
        filepath: str,
        analyzers: Optional[List[str]] = None,
        exclude: Optional[List[str]] = None,
    ) -> Snapshot:
        """Capture snapshot of an XCOFF file."""
        from .analyzers import get_all_analyzers

        path = Path(filepath)
        stat = path.stat()

        # Validate first
        validator = XCOFFValidator()
        validation = validator.validate(filepath)

        snapshot = Snapshot(
            filepath=str(path.resolve()),
            timestamp=datetime.now().isoformat(),
            file_size=stat.st_size,
            file_mtime=datetime.fromtimestamp(stat.st_mtime).isoformat(),
            file_type=validation.file_type or "unknown",
        )

        # Get analyzers to run
        all_analyzers = get_all_analyzers()

        if analyzers:
            selected = {n: all_analyzers[n] for n in analyzers if n in all_analyzers}
        else:
            selected = all_analyzers

        if exclude:
            selected = {n: a for n, a in selected.items() if n not in exclude}

        # Run each analyzer
        for name, analyzer in selected.items():
            if self.verbose:
                print(f"Running analyzer: {name}", file=__import__("sys").stderr)

            try:
                result = analyzer.analyze(filepath, timeout=self.timeout)
                snapshot.results[name] = result
            except Exception as e:
                snapshot.results[name] = AnalysisResult(
                    analyzer_name=name,
                    success=False,
                    error=str(e),
                )

        return snapshot


class ComparisonEngine:
    """Compare two snapshots."""

    def compare(self, snap1: Snapshot, snap2: Snapshot) -> ComparisonResult:
        """Compare two snapshots."""
        diffs = {}
        has_differences = False

        # Compare file metadata
        if snap1.file_size != snap2.file_size:
            diffs["_metadata"] = {
                "file_size": {"old": snap1.file_size, "new": snap2.file_size}
            }
            has_differences = True

        # Compare each analyzer result
        all_analyzers = set(snap1.results.keys()) | set(snap2.results.keys())

        for name in all_analyzers:
            r1 = snap1.results.get(name)
            r2 = snap2.results.get(name)

            if r1 is None:
                diffs[name] = {"status": "added"}
                has_differences = True
            elif r2 is None:
                diffs[name] = {"status": "removed"}
                has_differences = True
            elif r1.data != r2.data:
                diff = self._diff_data(name, r1.data, r2.data)
                if diff:
                    diffs[name] = diff
                    has_differences = True

        summary = self._build_summary(snap1, snap2, diffs)

        return ComparisonResult(
            filepath1=snap1.filepath,
            filepath2=snap2.filepath,
            has_differences=has_differences,
            summary=summary,
            analyzer_diffs=diffs,
        )

    def _diff_data(
        self, analyzer: str, data1: Dict, data2: Dict
    ) -> Optional[Dict[str, Any]]:
        """Diff analyzer data."""
        if analyzer == "what":
            return self._diff_what(data1, data2)
        elif analyzer == "dump-h":
            return self._diff_sections(data1, data2)
        elif analyzer == "dump-T":
            return self._diff_loader(data1, data2)
        else:
            # Generic diff
            if data1 != data2:
                return {"changed": True, "old": data1, "new": data2}
            return None

    def _diff_what(self, data1: Dict, data2: Dict) -> Optional[Dict[str, Any]]:
        """Diff what strings."""
        strings1 = set(data1.get("strings", []))
        strings2 = set(data2.get("strings", []))

        added = strings2 - strings1
        removed = strings1 - strings2

        if added or removed:
            return {
                "added": list(added),
                "removed": list(removed),
                "unchanged": len(strings1 & strings2),
            }
        return None

    def _diff_sections(self, data1: Dict, data2: Dict) -> Optional[Dict[str, Any]]:
        """Diff section headers."""
        secs1 = {s["name"]: s for s in data1.get("sections", [])}
        secs2 = {s["name"]: s for s in data2.get("sections", [])}

        changes = {}
        all_names = set(secs1.keys()) | set(secs2.keys())

        for name in all_names:
            s1 = secs1.get(name)
            s2 = secs2.get(name)

            if s1 is None:
                changes[name] = {"status": "added", "new": s2}
            elif s2 is None:
                changes[name] = {"status": "removed", "old": s1}
            elif s1 != s2:
                changes[name] = {"status": "modified", "old": s1, "new": s2}

        return changes if changes else None

    def _diff_loader(self, data1: Dict, data2: Dict) -> Optional[Dict[str, Any]]:
        """Diff loader symbols."""
        imp1 = set(s.get("name", "") for s in data1.get("imports", []))
        imp2 = set(s.get("name", "") for s in data2.get("imports", []))
        exp1 = set(s.get("name", "") for s in data1.get("exports", []))
        exp2 = set(s.get("name", "") for s in data2.get("exports", []))

        changes = {}

        if imp1 != imp2:
            changes["imports"] = {
                "added": list(imp2 - imp1),
                "removed": list(imp1 - imp2),
            }

        if exp1 != exp2:
            changes["exports"] = {
                "added": list(exp2 - exp1),
                "removed": list(exp1 - exp2),
            }

        return changes if changes else None

    def _build_summary(
        self, snap1: Snapshot, snap2: Snapshot, diffs: Dict
    ) -> str:
        """Build summary string."""
        if not diffs:
            return "No differences"

        parts = []
        for name, diff in diffs.items():
            if name == "_metadata":
                parts.append("file size changed")
            elif isinstance(diff, dict):
                if "added" in diff and "removed" in diff:
                    parts.append(f"{name}: +{len(diff['added'])} -{len(diff['removed'])}")
                elif "status" in diff:
                    parts.append(f"{name}: {diff['status']}")
                else:
                    parts.append(f"{name}: changed")

        return "; ".join(parts) if parts else "differences found"
