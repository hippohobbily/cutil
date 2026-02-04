#!/usr/bin/env python3
"""
xcoff_snapshot.py - Generate and compare XCOFF object snapshots

Captures metadata from XCOFF objects using AIX/PASE commands:
  - what:    SCCS identification strings (source versions, build info)
  - dump -h: Section headers (sizes, addresses, flags)
  - dump -T: Loader section symbols (imports/exports)

Usage:
  ./xcoff_snapshot.py snapshot <object_file> [-o output.json]
  ./xcoff_snapshot.py compare <snapshot1.json> <snapshot2.json>
  ./xcoff_snapshot.py diff <object1> <object2>

Requires: AIX or IBM i PASE environment with dump and what commands
"""

import subprocess
import json
import sys
import os
import re
import argparse
from datetime import datetime
from typing import Dict, List, Optional, Any


class XCOFFSnapshot:
    """Captures and stores XCOFF object metadata."""

    def __init__(self, filepath: str, bits: int = 64):
        self.filepath = filepath
        self.filename = os.path.basename(filepath)
        self.bits = bits  # 32 or 64
        self.timestamp = datetime.now().isoformat()

        self.what_strings: List[str] = []
        self.sections: List[Dict[str, Any]] = []
        self.loader_symbols: List[Dict[str, Any]] = []
        self.imports: List[Dict[str, str]] = []
        self.exports: List[Dict[str, Any]] = []

        self.errors: List[str] = []

    def run_command(self, cmd: List[str]) -> tuple[int, str, str]:
        """Run a shell command and return (returncode, stdout, stderr)."""
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=60
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return -1, "", "Command timed out"
        except FileNotFoundError:
            return -1, "", f"Command not found: {cmd[0]}"
        except Exception as e:
            return -1, "", str(e)

    def capture_what(self) -> None:
        """
        Capture SCCS identification strings using 'what' command.

        The 'what' command searches for @(#) patterns which typically
        contain source file versions, build timestamps, and other metadata.

        Example output:
            /path/to/file:
                    version.c 1.5 2024/01/15 10:30:00
                    module.c 2.1 2024/01/20 14:22:33
        """
        rc, stdout, stderr = self.run_command(["what", self.filepath])

        if rc != 0:
            self.errors.append(f"what command failed: {stderr}")
            return

        # Parse what output - skip the filename line, capture indented strings
        lines = stdout.strip().split('\n')
        for line in lines:
            # Skip empty lines and the filename header
            if not line.strip() or line.strip().endswith(':'):
                continue
            # Capture the identification string (typically indented with tabs)
            string = line.strip()
            if string:
                self.what_strings.append(string)

    def capture_sections(self) -> None:
        """
        Capture section headers using 'dump -h' command.

        Example output format:
                                ***Section Headers***
        Idx  Name      Size      VMA       LMA       File off  Algn
          0  .text     00001234  10000200  10000200  00000200  2**4
          1  .data     00000100  20000000  20000000  00001434  2**3
          2  .bss      00000050  20000100  20000100  00000000  2**2

        Or AIX format:
        			***Section Headers***
        [Index]	Name	Physical Address  Virtual Address  Size
        	Offset	Alignment  Relocation  Line Numbers  Flags
        [  0]	.text	0x0000000000000000  0x0000000010000128  0x00000368
        	0x00000128	   2**2     0x00000000     0x00000000  0x0020
        """
        x_flag = f"-X{self.bits}"
        rc, stdout, stderr = self.run_command(["dump", x_flag, "-h", self.filepath])

        if rc != 0:
            self.errors.append(f"dump -h failed: {stderr}")
            return

        self._parse_section_headers(stdout)

    def _parse_section_headers(self, output: str) -> None:
        """Parse section headers from dump -h output."""
        lines = output.strip().split('\n')

        # State machine to parse the varying formats
        in_headers = False
        current_section = {}

        for line in lines:
            # Skip empty lines
            if not line.strip():
                continue

            # Detect section headers marker
            if 'Section Header' in line or 'Idx' in line:
                in_headers = True
                continue

            if not in_headers:
                continue

            # Try to parse AIX format: [Index] Name ...
            match = re.match(r'\[\s*(\d+)\]\s+(\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)', line)
            if match:
                if current_section:
                    self.sections.append(current_section)
                current_section = {
                    'index': int(match.group(1)),
                    'name': match.group(2),
                    'physical_addr': match.group(3),
                    'virtual_addr': match.group(4),
                    'size': match.group(5),
                    'size_decimal': int(match.group(5), 16)
                }
                continue

            # Parse continuation line (offset, alignment, flags)
            if current_section and line.strip().startswith('0x'):
                parts = line.split()
                if len(parts) >= 5:
                    current_section['offset'] = parts[0]
                    current_section['alignment'] = parts[1] if len(parts) > 1 else ''
                    current_section['relocation'] = parts[2] if len(parts) > 2 else ''
                    current_section['line_numbers'] = parts[3] if len(parts) > 3 else ''
                    current_section['flags'] = parts[4] if len(parts) > 4 else ''
                continue

            # Alternative simpler format parsing
            # Try: index name size vma lma offset align
            parts = line.split()
            if len(parts) >= 3 and parts[0].isdigit():
                section = {
                    'index': int(parts[0]),
                    'name': parts[1],
                    'size': parts[2] if len(parts) > 2 else '0',
                }
                if len(parts) > 3:
                    section['virtual_addr'] = parts[3]
                if len(parts) > 4:
                    section['physical_addr'] = parts[4]
                if len(parts) > 5:
                    section['offset'] = parts[5]

                # Convert hex size to decimal if needed
                try:
                    if section['size'].startswith('0x'):
                        section['size_decimal'] = int(section['size'], 16)
                    else:
                        section['size_decimal'] = int(section['size'], 16)
                except ValueError:
                    section['size_decimal'] = 0

                self.sections.append(section)

        # Don't forget the last section
        if current_section and current_section not in self.sections:
            self.sections.append(current_section)

    def capture_loader_symbols(self) -> None:
        """
        Capture loader section symbols using 'dump -T' command.

        The loader section contains:
        - Imported symbols (undefined, from shared libraries)
        - Exported symbols (defined, available to other modules)

        Example output:
        [Index]	Value      Scn   IMEX Sclass   Type           IMPid Name
        [0]     0x00000000 undef IMP   DS EXTref    libc.a(shr.o) printf
        [1]     0x10000128 .text EXP   DS SECdef    [noIMid]      main
        """
        x_flag = f"-X{self.bits}"
        rc, stdout, stderr = self.run_command(["dump", x_flag, "-Tv", self.filepath])

        if rc != 0:
            # Try without -v
            rc, stdout, stderr = self.run_command(["dump", x_flag, "-T", self.filepath])
            if rc != 0:
                self.errors.append(f"dump -T failed: {stderr}")
                return

        self._parse_loader_symbols(stdout)

    def _parse_loader_symbols(self, output: str) -> None:
        """Parse loader section symbols from dump -T output."""
        lines = output.strip().split('\n')

        in_symbols = False
        in_imports = False

        for line in lines:
            if not line.strip():
                continue

            # Detect symbol table section
            if 'Symbol' in line and 'Table' in line:
                in_symbols = True
                continue

            # Detect import file strings section
            if 'Import File Strings' in line:
                in_imports = True
                in_symbols = False
                continue

            # Parse import file strings
            if in_imports:
                # Format: INDEX PATH BASE MEMBER
                # Or just library names
                parts = line.split()
                if len(parts) >= 2:
                    import_entry = {
                        'index': parts[0] if parts[0].isdigit() else '',
                        'path': parts[1] if len(parts) > 1 else '',
                        'base': parts[2] if len(parts) > 2 else '',
                        'member': parts[3] if len(parts) > 3 else ''
                    }
                    # Also store the raw line for reference
                    import_entry['raw'] = line.strip()
                    self.imports.append(import_entry)
                continue

            if not in_symbols:
                continue

            # Parse symbol entry
            # [Index] Value Scn IMEX Sclass Type IMPid Name
            match = re.match(
                r'\[\s*(\d+)\]\s+'           # [Index]
                r'(0x[0-9a-fA-F]+)\s+'       # Value
                r'(\S+)\s+'                   # Scn (section: .text, undef, etc)
                r'(\S+)\s+'                   # IMEX (IMP/EXP)
                r'(\S+)\s+'                   # Sclass
                r'(\S+)\s+'                   # Type
                r'(\S+)\s+'                   # IMPid
                r'(\S+)',                     # Name
                line
            )

            if match:
                symbol = {
                    'index': int(match.group(1)),
                    'value': match.group(2),
                    'section': match.group(3),
                    'imex': match.group(4),      # IMP or EXP
                    'sclass': match.group(5),
                    'type': match.group(6),
                    'impid': match.group(7),
                    'name': match.group(8)
                }
                self.loader_symbols.append(symbol)

                # Categorize as import or export
                if symbol['imex'] == 'IMP' or symbol['section'] == 'undef':
                    self.imports.append({
                        'name': symbol['name'],
                        'source': symbol['impid'],
                        'type': symbol['type']
                    })
                elif symbol['imex'] == 'EXP':
                    self.exports.append({
                        'name': symbol['name'],
                        'value': symbol['value'],
                        'section': symbol['section'],
                        'type': symbol['type']
                    })
                continue

            # Simpler format fallback
            parts = line.split()
            if len(parts) >= 4:
                symbol = {
                    'raw': line.strip(),
                    'parts': parts
                }
                self.loader_symbols.append(symbol)

    def capture_all(self) -> None:
        """Capture all metadata from the XCOFF object."""
        print(f"[INFO] Capturing snapshot of: {self.filepath}")
        print(f"[INFO] Mode: {self.bits}-bit")

        print("[CMD] what", self.filepath)
        self.capture_what()
        print(f"  -> Found {len(self.what_strings)} identification strings")

        print(f"[CMD] dump -X{self.bits} -h", self.filepath)
        self.capture_sections()
        print(f"  -> Found {len(self.sections)} sections")

        print(f"[CMD] dump -X{self.bits} -Tv", self.filepath)
        self.capture_loader_symbols()
        print(f"  -> Found {len(self.loader_symbols)} loader symbols")

        if self.errors:
            print("[WARN] Errors encountered:")
            for err in self.errors:
                print(f"  - {err}")

    def to_dict(self) -> Dict[str, Any]:
        """Convert snapshot to dictionary."""
        return {
            'metadata': {
                'filepath': self.filepath,
                'filename': self.filename,
                'bits': self.bits,
                'snapshot_time': self.timestamp,
                'errors': self.errors
            },
            'what_strings': self.what_strings,
            'sections': self.sections,
            'loader_symbols': self.loader_symbols,
            'imports': self.imports,
            'exports': self.exports,
            'summary': {
                'what_count': len(self.what_strings),
                'section_count': len(self.sections),
                'symbol_count': len(self.loader_symbols),
                'import_count': len(self.imports),
                'export_count': len(self.exports),
                'total_section_size': sum(s.get('size_decimal', 0) for s in self.sections)
            }
        }

    def to_json(self, indent: int = 2) -> str:
        """Convert snapshot to JSON string."""
        return json.dumps(self.to_dict(), indent=indent)

    def save(self, output_path: str) -> None:
        """Save snapshot to JSON file."""
        with open(output_path, 'w') as f:
            f.write(self.to_json())
        print(f"[INFO] Snapshot saved to: {output_path}")

    @classmethod
    def from_json(cls, json_path: str) -> 'XCOFFSnapshot':
        """Load snapshot from JSON file."""
        with open(json_path, 'r') as f:
            data = json.load(f)

        meta = data.get('metadata', {})
        snapshot = cls(meta.get('filepath', ''), meta.get('bits', 64))
        snapshot.timestamp = meta.get('snapshot_time', '')
        snapshot.errors = meta.get('errors', [])
        snapshot.what_strings = data.get('what_strings', [])
        snapshot.sections = data.get('sections', [])
        snapshot.loader_symbols = data.get('loader_symbols', [])
        snapshot.imports = data.get('imports', [])
        snapshot.exports = data.get('exports', [])

        return snapshot


class XCOFFCompare:
    """Compare two XCOFF snapshots."""

    def __init__(self, snap1: XCOFFSnapshot, snap2: XCOFFSnapshot):
        self.snap1 = snap1
        self.snap2 = snap2
        self.differences: Dict[str, Any] = {}

    def compare_what_strings(self) -> Dict[str, Any]:
        """Compare SCCS identification strings."""
        set1 = set(self.snap1.what_strings)
        set2 = set(self.snap2.what_strings)

        return {
            'only_in_first': sorted(list(set1 - set2)),
            'only_in_second': sorted(list(set2 - set1)),
            'common': sorted(list(set1 & set2)),
            'changed': len(set1) != len(set2) or set1 != set2
        }

    def compare_sections(self) -> Dict[str, Any]:
        """Compare section headers."""
        # Index sections by name
        sects1 = {s['name']: s for s in self.snap1.sections}
        sects2 = {s['name']: s for s in self.snap2.sections}

        names1 = set(sects1.keys())
        names2 = set(sects2.keys())

        added = names2 - names1
        removed = names1 - names2
        common = names1 & names2

        # Check for size/address changes in common sections
        modified = []
        for name in common:
            s1 = sects1[name]
            s2 = sects2[name]

            changes = {}
            for key in ['size', 'size_decimal', 'virtual_addr', 'physical_addr', 'flags']:
                v1 = s1.get(key)
                v2 = s2.get(key)
                if v1 != v2:
                    changes[key] = {'old': v1, 'new': v2}

            if changes:
                modified.append({
                    'name': name,
                    'changes': changes
                })

        return {
            'added': sorted(list(added)),
            'removed': sorted(list(removed)),
            'modified': modified,
            'changed': bool(added or removed or modified)
        }

    def compare_symbols(self) -> Dict[str, Any]:
        """Compare loader symbols."""
        # Extract symbol names
        def get_symbol_names(symbols):
            names = set()
            for s in symbols:
                if 'name' in s:
                    names.add(s['name'])
                elif 'parts' in s and len(s['parts']) > 0:
                    names.add(s['parts'][-1])  # Last part is usually the name
            return names

        names1 = get_symbol_names(self.snap1.loader_symbols)
        names2 = get_symbol_names(self.snap2.loader_symbols)

        return {
            'added': sorted(list(names2 - names1)),
            'removed': sorted(list(names1 - names2)),
            'common_count': len(names1 & names2),
            'changed': names1 != names2
        }

    def compare_exports(self) -> Dict[str, Any]:
        """Compare exported symbols."""
        exp1 = {e['name']: e for e in self.snap1.exports if 'name' in e}
        exp2 = {e['name']: e for e in self.snap2.exports if 'name' in e}

        names1 = set(exp1.keys())
        names2 = set(exp2.keys())

        added = names2 - names1
        removed = names1 - names2

        # Check for value changes in common exports
        modified = []
        for name in names1 & names2:
            e1 = exp1[name]
            e2 = exp2[name]
            if e1.get('value') != e2.get('value'):
                modified.append({
                    'name': name,
                    'old_value': e1.get('value'),
                    'new_value': e2.get('value')
                })

        return {
            'added': sorted(list(added)),
            'removed': sorted(list(removed)),
            'modified': modified,
            'changed': bool(added or removed or modified)
        }

    def compare_all(self) -> Dict[str, Any]:
        """Perform full comparison."""
        self.differences = {
            'files': {
                'first': self.snap1.filename,
                'second': self.snap2.filename
            },
            'what_strings': self.compare_what_strings(),
            'sections': self.compare_sections(),
            'symbols': self.compare_symbols(),
            'exports': self.compare_exports(),
            'summary': {
                'first': self.snap1.to_dict()['summary'],
                'second': self.snap2.to_dict()['summary']
            }
        }

        # Overall changed flag
        self.differences['changed'] = any([
            self.differences['what_strings']['changed'],
            self.differences['sections']['changed'],
            self.differences['symbols']['changed'],
            self.differences['exports']['changed']
        ])

        return self.differences

    def print_report(self) -> None:
        """Print human-readable comparison report."""
        if not self.differences:
            self.compare_all()

        d = self.differences

        print("=" * 70)
        print("XCOFF Object Comparison Report")
        print("=" * 70)
        print(f"File 1: {d['files']['first']}")
        print(f"File 2: {d['files']['second']}")
        print()

        # What strings
        print("-" * 70)
        print("IDENTIFICATION STRINGS (what)")
        print("-" * 70)
        ws = d['what_strings']
        if ws['only_in_first']:
            print("  Only in first:")
            for s in ws['only_in_first']:
                print(f"    - {s}")
        if ws['only_in_second']:
            print("  Only in second:")
            for s in ws['only_in_second']:
                print(f"    + {s}")
        if not ws['changed']:
            print("  No differences")
        print()

        # Sections
        print("-" * 70)
        print("SECTIONS (dump -h)")
        print("-" * 70)
        sec = d['sections']
        if sec['added']:
            print("  Added sections:")
            for s in sec['added']:
                print(f"    + {s}")
        if sec['removed']:
            print("  Removed sections:")
            for s in sec['removed']:
                print(f"    - {s}")
        if sec['modified']:
            print("  Modified sections:")
            for m in sec['modified']:
                print(f"    ~ {m['name']}:")
                for key, vals in m['changes'].items():
                    print(f"        {key}: {vals['old']} -> {vals['new']}")
        if not sec['changed']:
            print("  No differences")
        print()

        # Symbols
        print("-" * 70)
        print("LOADER SYMBOLS (dump -T)")
        print("-" * 70)
        sym = d['symbols']
        if sym['added']:
            print(f"  Added symbols ({len(sym['added'])}):")
            for s in sym['added'][:20]:  # Limit output
                print(f"    + {s}")
            if len(sym['added']) > 20:
                print(f"    ... and {len(sym['added']) - 20} more")
        if sym['removed']:
            print(f"  Removed symbols ({len(sym['removed'])}):")
            for s in sym['removed'][:20]:
                print(f"    - {s}")
            if len(sym['removed']) > 20:
                print(f"    ... and {len(sym['removed']) - 20} more")
        if not sym['changed']:
            print("  No differences")
        print()

        # Exports
        print("-" * 70)
        print("EXPORTS")
        print("-" * 70)
        exp = d['exports']
        if exp['added']:
            print("  Added exports:")
            for s in exp['added']:
                print(f"    + {s}")
        if exp['removed']:
            print("  Removed exports:")
            for s in exp['removed']:
                print(f"    - {s}")
        if exp['modified']:
            print("  Modified exports (address changed):")
            for m in exp['modified']:
                print(f"    ~ {m['name']}: {m['old_value']} -> {m['new_value']}")
        if not exp['changed']:
            print("  No differences")
        print()

        # Summary
        print("-" * 70)
        print("SUMMARY")
        print("-" * 70)
        s1 = d['summary']['first']
        s2 = d['summary']['second']
        print(f"  {'Metric':<25} {'File 1':>12} {'File 2':>12} {'Delta':>12}")
        print(f"  {'-'*25} {'-'*12} {'-'*12} {'-'*12}")
        print(f"  {'What strings':<25} {s1['what_count']:>12} {s2['what_count']:>12} {s2['what_count']-s1['what_count']:>+12}")
        print(f"  {'Sections':<25} {s1['section_count']:>12} {s2['section_count']:>12} {s2['section_count']-s1['section_count']:>+12}")
        print(f"  {'Loader symbols':<25} {s1['symbol_count']:>12} {s2['symbol_count']:>12} {s2['symbol_count']-s1['symbol_count']:>+12}")
        print(f"  {'Total section size':<25} {s1['total_section_size']:>12} {s2['total_section_size']:>12} {s2['total_section_size']-s1['total_section_size']:>+12}")
        print()

        print("=" * 70)
        if d['changed']:
            print("RESULT: Objects are DIFFERENT")
        else:
            print("RESULT: Objects are IDENTICAL (in analyzed aspects)")
        print("=" * 70)


def main():
    parser = argparse.ArgumentParser(
        description='XCOFF object snapshot and comparison tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s snapshot /usr/lib/libc.a -o libc_v1.json
  %(prog)s snapshot /usr/lib/libc.a -o libc_v2.json
  %(prog)s compare libc_v1.json libc_v2.json

  %(prog)s diff /path/to/old/prog /path/to/new/prog
  %(prog)s diff -32 old.o new.o  # 32-bit objects
"""
    )

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Snapshot command
    snap_parser = subparsers.add_parser('snapshot', help='Create snapshot of XCOFF object')
    snap_parser.add_argument('object', help='Path to XCOFF object file')
    snap_parser.add_argument('-o', '--output', help='Output JSON file (default: <object>.snapshot.json)')
    snap_parser.add_argument('-32', dest='bits32', action='store_true', help='32-bit object')
    snap_parser.add_argument('-64', dest='bits64', action='store_true', help='64-bit object (default)')

    # Compare command
    cmp_parser = subparsers.add_parser('compare', help='Compare two snapshot files')
    cmp_parser.add_argument('snapshot1', help='First snapshot JSON file')
    cmp_parser.add_argument('snapshot2', help='Second snapshot JSON file')
    cmp_parser.add_argument('-o', '--output', help='Output comparison to JSON file')

    # Diff command (snapshot + compare in one step)
    diff_parser = subparsers.add_parser('diff', help='Direct diff of two XCOFF objects')
    diff_parser.add_argument('object1', help='First XCOFF object')
    diff_parser.add_argument('object2', help='Second XCOFF object')
    diff_parser.add_argument('-32', dest='bits32', action='store_true', help='32-bit objects')
    diff_parser.add_argument('-64', dest='bits64', action='store_true', help='64-bit objects (default)')
    diff_parser.add_argument('-o', '--output', help='Output comparison to JSON file')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    if args.command == 'snapshot':
        bits = 32 if args.bits32 else 64
        snap = XCOFFSnapshot(args.object, bits)
        snap.capture_all()

        output = args.output or f"{args.object}.snapshot.json"
        snap.save(output)

        print()
        print(json.dumps(snap.to_dict()['summary'], indent=2))

    elif args.command == 'compare':
        snap1 = XCOFFSnapshot.from_json(args.snapshot1)
        snap2 = XCOFFSnapshot.from_json(args.snapshot2)

        compare = XCOFFCompare(snap1, snap2)
        compare.compare_all()
        compare.print_report()

        if args.output:
            with open(args.output, 'w') as f:
                json.dump(compare.differences, f, indent=2)
            print(f"\n[INFO] Comparison saved to: {args.output}")

    elif args.command == 'diff':
        bits = 32 if args.bits32 else 64

        print(f"Creating snapshot of: {args.object1}")
        snap1 = XCOFFSnapshot(args.object1, bits)
        snap1.capture_all()

        print()
        print(f"Creating snapshot of: {args.object2}")
        snap2 = XCOFFSnapshot(args.object2, bits)
        snap2.capture_all()

        print()
        compare = XCOFFCompare(snap1, snap2)
        compare.compare_all()
        compare.print_report()

        if args.output:
            with open(args.output, 'w') as f:
                json.dump(compare.differences, f, indent=2)
            print(f"\n[INFO] Comparison saved to: {args.output}")


if __name__ == '__main__':
    main()
