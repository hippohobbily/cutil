#!/usr/bin/env python3
"""
Storage for named snapshots.

Structure:
~/.xcoffscandb/
├── snapshots/
│   └── <name>/
│       └── path/to/file.json
└── registry.txt
"""

import os
import json
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, Any

from .core import Snapshot


class Storage:
    """Storage manager for named snapshots."""

    def __init__(self, db_path: Optional[Path] = None):
        self.db_path = db_path or Path.home() / ".xcoffscandb"

    def _ensure_dirs(self, name: str) -> Path:
        """Ensure snapshot directory exists."""
        snap_dir = self.db_path / "snapshots" / name
        snap_dir.mkdir(parents=True, exist_ok=True)
        return snap_dir

    def _filepath_to_relpath(self, filepath: str) -> str:
        """Convert absolute filepath to relative storage path."""
        # /usr/bin/ls -> usr/bin/ls.json
        rel = filepath.lstrip("/").lstrip("\\")
        # Handle Windows paths too
        rel = rel.replace(":", "")
        return rel + ".json"

    def _relpath_to_filepath(self, relpath: str) -> str:
        """Convert relative storage path back to filepath."""
        # usr/bin/ls.json -> /usr/bin/ls
        fp = relpath.rstrip(".json")
        return "/" + fp

    def store(self, name: str, snapshot: Snapshot) -> Path:
        """Store a snapshot with the given name."""
        snap_dir = self._ensure_dirs(name)

        # Set the name on the snapshot
        snapshot.name = name

        # Compute storage path
        rel_path = self._filepath_to_relpath(snapshot.filepath)
        json_path = snap_dir / rel_path

        # Ensure parent directory
        json_path.parent.mkdir(parents=True, exist_ok=True)

        # Write snapshot JSON
        with open(json_path, "w") as f:
            json.dump(snapshot.to_dict(), f, indent=2)

        # Update registry
        self._update_registry(name)

        return json_path

    def load(self, name: str, filepath: str) -> Optional[Snapshot]:
        """Load a snapshot by name and filepath."""
        snap_dir = self.db_path / "snapshots" / name
        rel_path = self._filepath_to_relpath(filepath)
        json_path = snap_dir / rel_path

        if not json_path.exists():
            return None

        try:
            with open(json_path, "r") as f:
                data = json.load(f)
            return Snapshot.from_dict(data)
        except (json.JSONDecodeError, KeyError) as e:
            return None

    def list_snapshots(self) -> Dict[str, Dict[str, Any]]:
        """List all named snapshots."""
        registry_path = self.db_path / "registry.txt"
        snapshots = {}

        if registry_path.exists():
            with open(registry_path, "r") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("#") or not line:
                        continue
                    parts = line.split("\t")
                    if len(parts) >= 3:
                        snapshots[parts[0]] = {
                            "created": parts[1],
                            "file_count": int(parts[2]),
                            "description": parts[3] if len(parts) > 3 else "",
                        }

        # Also check for directories not in registry
        snap_base = self.db_path / "snapshots"
        if snap_base.exists():
            for d in snap_base.iterdir():
                if d.is_dir() and d.name not in snapshots:
                    file_count = sum(1 for _ in d.rglob("*.json"))
                    snapshots[d.name] = {
                        "created": datetime.fromtimestamp(d.stat().st_mtime).isoformat(),
                        "file_count": file_count,
                        "description": "",
                    }

        return snapshots

    def list_files(self, name: str) -> List[str]:
        """List all files in a named snapshot."""
        snap_dir = self.db_path / "snapshots" / name

        if not snap_dir.exists():
            return []

        files = []
        for json_path in snap_dir.rglob("*.json"):
            rel = json_path.relative_to(snap_dir)
            filepath = self._relpath_to_filepath(str(rel))
            files.append(filepath)

        return files

    def delete_snapshot(self, name: str) -> bool:
        """Delete a named snapshot."""
        snap_dir = self.db_path / "snapshots" / name

        if not snap_dir.exists():
            return False

        import shutil
        shutil.rmtree(snap_dir)

        # Update registry
        self._remove_from_registry(name)

        return True

    def _update_registry(self, name: str) -> None:
        """Update registry file with snapshot info."""
        registry_path = self.db_path / "registry.txt"
        snap_dir = self.db_path / "snapshots" / name

        # Count files
        file_count = sum(1 for _ in snap_dir.rglob("*.json"))

        # Read existing entries
        entries = {}
        if registry_path.exists():
            with open(registry_path, "r") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("#") or not line:
                        continue
                    parts = line.split("\t")
                    if len(parts) >= 3:
                        entries[parts[0]] = parts

        # Update or add entry
        now = datetime.now().isoformat()
        if name in entries:
            entries[name][2] = str(file_count)
        else:
            entries[name] = [name, now, str(file_count), ""]

        # Write registry
        self._write_registry(entries)

    def _remove_from_registry(self, name: str) -> None:
        """Remove snapshot from registry."""
        registry_path = self.db_path / "registry.txt"

        if not registry_path.exists():
            return

        entries = {}
        with open(registry_path, "r") as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or not line:
                    continue
                parts = line.split("\t")
                if len(parts) >= 3 and parts[0] != name:
                    entries[parts[0]] = parts

        self._write_registry(entries)

    def _write_registry(self, entries: Dict[str, List[str]]) -> None:
        """Write registry file."""
        registry_path = self.db_path / "registry.txt"
        registry_path.parent.mkdir(parents=True, exist_ok=True)

        with open(registry_path, "w") as f:
            f.write("# XCOFF Scan Registry\n")
            f.write("# NAME<TAB>CREATED<TAB>FILE_COUNT<TAB>DESCRIPTION\n")
            for name in sorted(entries.keys()):
                f.write("\t".join(entries[name]) + "\n")
