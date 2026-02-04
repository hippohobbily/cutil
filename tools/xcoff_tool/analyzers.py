#!/usr/bin/env python3
"""
XCOFF analyzers: what, dump -h, dump -T
"""

import subprocess
import shutil
import re
from abc import ABC, abstractmethod
from typing import Dict, List, Optional, Any

from .core import AnalysisResult


class Analyzer(ABC):
    """Base class for analyzers."""

    @property
    @abstractmethod
    def name(self) -> str:
        """Analyzer name."""
        pass

    @property
    @abstractmethod
    def description(self) -> str:
        """Short description."""
        pass

    @property
    @abstractmethod
    def required_commands(self) -> List[str]:
        """Commands required by this analyzer."""
        pass

    @abstractmethod
    def analyze(self, filepath: str, timeout: int = 300) -> AnalysisResult:
        """Run analysis on file."""
        pass

    def check_requirements(self) -> List[str]:
        """Check if required commands are available."""
        missing = []
        for cmd in self.required_commands:
            if not shutil.which(cmd):
                missing.append(cmd)
        return missing

    def _run_command(
        self, cmd: List[str], timeout: int = 300
    ) -> subprocess.CompletedProcess:
        """Run command and return result."""
        return subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
        )


class WhatAnalyzer(Analyzer):
    """Analyzer using 'what' command to extract SCCS identification strings."""

    @property
    def name(self) -> str:
        return "what"

    @property
    def description(self) -> str:
        return "Extract @(#) identification strings"

    @property
    def required_commands(self) -> List[str]:
        return ["what"]

    def analyze(self, filepath: str, timeout: int = 300) -> AnalysisResult:
        """Run 'what' command and parse output."""
        try:
            result = self._run_command(["what", filepath], timeout)

            if result.returncode != 0 and not result.stdout:
                return AnalysisResult(
                    analyzer_name=self.name,
                    success=False,
                    error=result.stderr.strip() or f"Exit code {result.returncode}",
                )

            strings = self._parse_output(result.stdout)

            return AnalysisResult(
                analyzer_name=self.name,
                success=True,
                data={
                    "strings": strings,
                    "count": len(strings),
                },
            )

        except subprocess.TimeoutExpired:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=f"Timeout after {timeout}s",
            )
        except Exception as e:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=str(e),
            )

    def _parse_output(self, output: str) -> List[str]:
        """Parse what command output."""
        strings = []
        for line in output.splitlines():
            line = line.strip()
            # Skip filename header lines (end with :)
            if line and not line.endswith(":"):
                strings.append(line)
        return strings


class DumpHeadersAnalyzer(Analyzer):
    """Analyzer using 'dump -h' to show section headers."""

    @property
    def name(self) -> str:
        return "dump-h"

    @property
    def description(self) -> str:
        return "Display section headers"

    @property
    def required_commands(self) -> List[str]:
        return ["dump"]

    def analyze(self, filepath: str, timeout: int = 300) -> AnalysisResult:
        """Run 'dump -h' and parse output."""
        try:
            result = self._run_command(["dump", "-h", filepath], timeout)

            if result.returncode != 0:
                return AnalysisResult(
                    analyzer_name=self.name,
                    success=False,
                    error=result.stderr.strip() or f"Exit code {result.returncode}",
                )

            sections = self._parse_output(result.stdout)

            return AnalysisResult(
                analyzer_name=self.name,
                success=True,
                data={
                    "sections": sections,
                    "count": len(sections),
                    "raw": result.stdout,
                },
            )

        except subprocess.TimeoutExpired:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=f"Timeout after {timeout}s",
            )
        except Exception as e:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=str(e),
            )

    def _parse_output(self, output: str) -> List[Dict[str, Any]]:
        """Parse dump -h output."""
        sections = []
        in_section_table = False
        header_line = None

        for line in output.splitlines():
            line = line.strip()

            if not line:
                continue

            # Look for section header table
            if "Idx" in line and "Name" in line:
                in_section_table = True
                header_line = line
                continue

            if in_section_table:
                # Parse section line
                # Format varies but typically:
                # Idx Name      Size     VMA      LMA      File off  Algn  Flags
                parts = line.split()
                if len(parts) >= 2 and parts[0].isdigit():
                    section = {
                        "index": int(parts[0]),
                        "name": parts[1],
                    }

                    # Try to extract more fields
                    if len(parts) >= 3:
                        try:
                            section["size"] = parts[2]
                        except:
                            pass
                    if len(parts) >= 4:
                        section["vma"] = parts[3]
                    if len(parts) >= 5:
                        section["lma"] = parts[4]
                    if len(parts) >= 6:
                        section["file_offset"] = parts[5]

                    sections.append(section)

        # Alternative parsing for AIX dump format
        if not sections:
            sections = self._parse_aix_format(output)

        return sections

    def _parse_aix_format(self, output: str) -> List[Dict[str, Any]]:
        """Parse AIX-specific dump -h format."""
        sections = []

        # AIX format shows sections differently
        # Look for section name patterns
        section_pattern = re.compile(
            r"^\s*(\d+)\s+(\.\w+|\w+)\s+([0-9a-fA-Fx]+)\s+([0-9a-fA-Fx]+)",
            re.MULTILINE,
        )

        for match in section_pattern.finditer(output):
            sections.append({
                "index": int(match.group(1)),
                "name": match.group(2),
                "size": match.group(3),
                "address": match.group(4),
            })

        return sections


class DumpLoaderAnalyzer(Analyzer):
    """Analyzer using 'dump -T' to show loader section symbols."""

    @property
    def name(self) -> str:
        return "dump-T"

    @property
    def description(self) -> str:
        return "Display loader section (imports/exports)"

    @property
    def required_commands(self) -> List[str]:
        return ["dump"]

    def analyze(self, filepath: str, timeout: int = 300) -> AnalysisResult:
        """Run 'dump -T' and parse output."""
        try:
            result = self._run_command(["dump", "-Tv", filepath], timeout)

            if result.returncode != 0:
                # dump -T may fail on non-executable objects
                return AnalysisResult(
                    analyzer_name=self.name,
                    success=False,
                    error=result.stderr.strip() or f"Exit code {result.returncode}",
                )

            imports, exports = self._parse_output(result.stdout)

            return AnalysisResult(
                analyzer_name=self.name,
                success=True,
                data={
                    "imports": imports,
                    "exports": exports,
                    "import_count": len(imports),
                    "export_count": len(exports),
                    "raw": result.stdout,
                },
            )

        except subprocess.TimeoutExpired:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=f"Timeout after {timeout}s",
            )
        except Exception as e:
            return AnalysisResult(
                analyzer_name=self.name,
                success=False,
                error=str(e),
            )

    def _parse_output(self, output: str) -> tuple:
        """Parse dump -Tv output."""
        imports = []
        exports = []

        for line in output.splitlines():
            line = line.strip()
            if not line:
                continue

            # Look for symbol entries
            # Format: [Index]  Value    Scn   IMEX Sclass  Type   IMPid   Name
            # IMP = import, EXP = export

            parts = line.split()
            if len(parts) < 4:
                continue

            # Check for IMP or EXP in the line
            if "IMP" in line or "EXTref" in line:
                # Import symbol
                name = self._extract_symbol_name(parts)
                if name:
                    imports.append({
                        "name": name,
                        "type": "IMP",
                        "raw": line,
                    })
            elif "EXP" in line or "SECdef" in line:
                # Export symbol
                name = self._extract_symbol_name(parts)
                if name:
                    address = self._extract_address(parts)
                    exports.append({
                        "name": name,
                        "type": "EXP",
                        "address": address,
                        "raw": line,
                    })

        return imports, exports

    def _extract_symbol_name(self, parts: List[str]) -> Optional[str]:
        """Extract symbol name from parsed line parts."""
        # Symbol name is typically the last field
        # But may have library prefix like libc.a(shr.o)
        for part in reversed(parts):
            # Skip common non-name fields
            if part in ("IMP", "EXP", "EXTref", "SECdef", "DS"):
                continue
            if part.startswith("0x") or part.startswith("["):
                continue
            if part.isdigit():
                continue
            # This might be the name
            if part and not part.startswith("."):
                return part
        return None

    def _extract_address(self, parts: List[str]) -> Optional[str]:
        """Extract address from parsed line parts."""
        for part in parts:
            if part.startswith("0x"):
                return part
        return None


# Registry of all analyzers
_ANALYZERS: Dict[str, Analyzer] = {}


def register_analyzer(analyzer: Analyzer) -> None:
    """Register an analyzer."""
    _ANALYZERS[analyzer.name] = analyzer


def get_all_analyzers() -> Dict[str, Analyzer]:
    """Get all registered analyzers."""
    if not _ANALYZERS:
        # Register built-in analyzers
        register_analyzer(WhatAnalyzer())
        register_analyzer(DumpHeadersAnalyzer())
        register_analyzer(DumpLoaderAnalyzer())

    return _ANALYZERS


def get_analyzer(name: str) -> Optional[Analyzer]:
    """Get analyzer by name."""
    return get_all_analyzers().get(name)
