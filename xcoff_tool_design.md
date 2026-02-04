# XCOFF Analysis Tool - Design Document

## 1. Overview

### 1.1 Purpose
A command-line tool for capturing, analyzing, and comparing XCOFF object files on AIX/IBM i PASE. Primary use cases:
- PTF validation (before/after binary comparison)
- Build verification (debug vs release, compiler differences)
- Regression detection (symbol changes, section drift)

### 1.2 Design Goals

| Priority | Goal | Rationale |
|----------|------|-----------|
| 1 | **Robustness** | Must handle malformed files, huge binaries, command failures |
| 2 | **Memory Efficiency** | XCOFF files can exceed 500MB; symbol tables can have millions of entries |
| 3 | **Extensibility** | Easy addition of new analyzers without modifying core code |
| 4 | **Performance** | Parallel analyzer execution, streaming output |
| 5 | **Usability** | Clear CLI, sensible defaults, good error messages |

---

## 2. Command-Line Interface

### 2.1 Command Structure

```
xcoff <command> [options] <arguments>

Commands:
    snapshot    Capture analysis snapshot of an XCOFF file
    compare     Compare two XCOFF files directly
    diff        Compare two previously saved snapshots
    info        Quick summary of XCOFF file
    list        List available analyzers / stored snapshots
    validate    Check if file is valid XCOFF
```

### 2.2 Global Options

```
Options:
    -v, --verbose           Increase verbosity (can stack: -vv, -vvv)
    -q, --quiet             Suppress non-essential output
    -o, --output FILE       Write output to FILE (default: stdout)
    -f, --format FORMAT     Output format: text, json, jsonl (default: text)
    --color / --no-color    Enable/disable colored output (auto-detect tty)
    --config FILE           Load configuration from FILE
    --log-file FILE         Write logs to FILE
    --log-level LEVEL       Log level: debug, info, warn, error
    --version               Show version and exit
    --help                  Show help and exit
```

### 2.3 Snapshot Command

```
xcoff snapshot [options] <xcoff-file>

Capture analysis snapshot of an XCOFF file.

Options:
    -a, --analyzers LIST    Comma-separated analyzer names (default: all)
                            Example: --analyzers what,dump-h,dump-T
    -x, --exclude LIST      Exclude these analyzers
    --save FILE             Save snapshot to FILE (JSON format)
    --timeout SECONDS       Per-analyzer timeout (default: 300)
    --max-symbols N         Limit symbol output to N entries (default: unlimited)
    --max-output-size BYTES Maximum output size per analyzer (default: 100MB)

Examples:
    xcoff snapshot /usr/bin/ls
    xcoff snapshot -a what,dump-h /usr/lib/libc.a
    xcoff snapshot --save before.json /usr/bin/myapp
    xcoff snapshot -f json /usr/bin/ls > snapshot.json
```

### 2.4 Compare Command

```
xcoff compare [options] <file1> <file2>

Compare two XCOFF files directly.

Options:
    -a, --analyzers LIST    Analyzers to use for comparison
    -x, --exclude LIST      Exclude these analyzers
    --ignore-timestamps     Ignore timestamp differences
    --ignore-addresses      Ignore address differences (for relocated binaries)
    --context N             Show N lines of context in diffs (default: 3)
    --summary-only          Show only summary, no detailed diff
    --fail-on-diff          Exit with code 1 if differences found
    --timeout SECONDS       Per-analyzer timeout (default: 300)

Examples:
    xcoff compare old/myapp new/myapp
    xcoff compare --summary-only v1.o v2.o
    xcoff compare -a dump-T --fail-on-diff lib1.a lib2.a
```

### 2.5 Diff Command

```
xcoff diff [options] <snapshot1.json> <snapshot2.json>

Compare two previously saved snapshots.

Options:
    --ignore-timestamps     Ignore timestamp differences
    --context N             Show N lines of context (default: 3)
    --summary-only          Show only summary
    --fail-on-diff          Exit with code 1 if differences found

Examples:
    xcoff diff before.json after.json
    xcoff diff --summary-only baseline.json current.json
```

### 2.6 Info Command

```
xcoff info [options] <xcoff-file>

Display quick summary of XCOFF file.

Options:
    --headers               Show all header details
    --sections              List section summary
    --symbols-count         Show symbol counts only

Examples:
    xcoff info /usr/bin/ls
    xcoff info --headers myprogram
```

### 2.7 List Command

```
xcoff list [options]

List available analyzers and their status.

Options:
    --check                 Verify required commands exist on system

Examples:
    xcoff list
    xcoff list --check
```

### 2.8 Validate Command

```
xcoff validate <xcoff-file>

Check if file is a valid XCOFF object.

Examples:
    xcoff validate /usr/bin/ls
    xcoff validate suspicious.o && echo "Valid XCOFF"
```

---

## 3. Architecture

### 3.1 High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              CLI Layer                                   │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │snapshot │ │compare  │ │  diff   │ │  info   │ │  list   │           │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘           │
└───────┼──────────┼──────────┼──────────┼──────────┼─────────────────────┘
        │          │          │          │          │
        ▼          ▼          ▼          ▼          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           Core Engine                                    │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐      │
│  │  SnapshotManager │  │ ComparisonEngine │  │  OutputFormatter │      │
│  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘      │
│           │                     │                     │                 │
│           ▼                     ▼                     ▼                 │
│  ┌──────────────────────────────────────────────────────────────┐      │
│  │                     Analyzer Registry                         │      │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐│      │
│  │  │  what   │ │ dump-h  │ │ dump-T  │ │ dump-t  │ │ custom  ││      │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘│      │
│  └──────────────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Infrastructure Layer                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │CommandRunner │  │StreamBuffer  │  │  FileValidator│  │ TempManager │ │
│  │ (subprocess) │  │ (memory mgmt)│  │  (XCOFF check)│  │ (cleanup)   │ │
│  └──────────────┘  └──────────────┘  └──────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Module Structure

```
xcoff_tool/
├── __init__.py
├── __main__.py              # Entry point: python -m xcoff_tool
├── cli.py                   # Argument parsing, command dispatch
├── config.py                # Configuration management
│
├── core/
│   ├── __init__.py
│   ├── engine.py            # SnapshotManager, ComparisonEngine
│   ├── snapshot.py          # Snapshot data structures
│   ├── comparison.py        # Comparison logic and results
│   └── registry.py          # Analyzer registration and discovery
│
├── analyzers/
│   ├── __init__.py
│   ├── base.py              # Abstract base classes
│   ├── what.py              # 'what' command analyzer
│   ├── dump_headers.py      # 'dump -h' analyzer
│   ├── dump_loader.py       # 'dump -T' analyzer
│   ├── dump_symbols.py      # 'dump -t' analyzer (future)
│   ├── strings.py           # 'strings' analyzer (future)
│   └── custom.py            # User-defined analyzer support
│
├── infra/
│   ├── __init__.py
│   ├── command.py           # Subprocess execution with streaming
│   ├── buffer.py            # Memory-efficient buffering
│   ├── validator.py         # XCOFF file validation
│   ├── tempfiles.py         # Temporary file management
│   └── signals.py           # Signal handling (Ctrl+C, etc.)
│
├── output/
│   ├── __init__.py
│   ├── formatter.py         # Base formatter
│   ├── text.py              # Plain text output
│   ├── json_fmt.py          # JSON output
│   └── diff.py              # Diff formatting
│
└── utils/
    ├── __init__.py
    ├── logging.py           # Logging configuration
    ├── errors.py            # Custom exceptions
    └── constants.py         # Magic numbers, limits, defaults
```

---

## 4. Memory Management Strategy

### 4.1 Problem Statement

XCOFF files can be extremely large:
- System libraries: 50-200 MB
- Large applications: 100-500 MB
- Symbol tables: Millions of entries (each entry ~50-200 bytes of output)

A naive implementation loading everything into memory would:
- Exhaust RAM on constrained systems
- Cause severe performance degradation from swapping
- Risk OOM kills

### 4.2 Memory Budget Model

```python
class MemoryBudget:
    """
    Configurable memory limits with sensible defaults.

    Default budget assumes 512MB available for the tool:
    - Command output buffer: 100MB max per analyzer
    - Symbol accumulator: 200MB max
    - Comparison working set: 100MB
    - Overhead/safety margin: 112MB
    """

    DEFAULT_LIMITS = {
        'per_analyzer_output': 100 * 1024 * 1024,    # 100 MB
        'symbol_accumulator': 200 * 1024 * 1024,     # 200 MB
        'comparison_working': 100 * 1024 * 1024,     # 100 MB
        'line_buffer': 64 * 1024,                    # 64 KB per line max
        'chunk_size': 8 * 1024,                      # 8 KB read chunks
    }
```

### 4.3 Streaming Command Output

**Problem:** `dump -t` on a large binary can produce gigabytes of output.

**Solution:** Stream-process subprocess output line-by-line.

```python
class StreamingCommandRunner:
    """
    Execute commands with streaming output processing.

    Key features:
    - Line-by-line processing (never loads full output)
    - Configurable line length limit (truncate long lines)
    - Output size limit with graceful cutoff
    - Timeout with SIGTERM/SIGKILL escalation
    - Memory monitoring during execution
    """

    def __init__(
        self,
        timeout: float = 300.0,
        max_line_length: int = 65536,
        max_total_output: int = 100 * 1024 * 1024,
        chunk_size: int = 8192,
    ):
        self.timeout = timeout
        self.max_line_length = max_line_length
        self.max_total_output = max_total_output
        self.chunk_size = chunk_size

    def run_streaming(
        self,
        cmd: List[str],
        line_callback: Callable[[str], None],
    ) -> CommandResult:
        """
        Execute command, calling line_callback for each output line.

        This allows the analyzer to:
        - Parse incrementally
        - Aggregate/summarize on-the-fly
        - Discard unneeded data immediately

        Memory usage stays constant regardless of output size.
        """
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,  # Unbuffered
        )

        total_bytes = 0
        truncated = False

        try:
            # Use selectors for non-blocking read with timeout
            with selectors.DefaultSelector() as sel:
                sel.register(process.stdout, selectors.EVENT_READ)
                sel.register(process.stderr, selectors.EVENT_READ)

                line_buffer = bytearray()
                deadline = time.monotonic() + self.timeout

                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError(f"Command timed out after {self.timeout}s")

                    ready = sel.select(timeout=min(remaining, 1.0))

                    if not ready and process.poll() is not None:
                        break

                    for key, _ in ready:
                        chunk = key.fileobj.read(self.chunk_size)
                        if not chunk:
                            sel.unregister(key.fileobj)
                            continue

                        if key.fileobj == process.stdout:
                            total_bytes += len(chunk)

                            if total_bytes > self.max_total_output:
                                truncated = True
                                # Graceful stop - don't read more
                                break

                            # Process complete lines
                            line_buffer.extend(chunk)
                            while b'\n' in line_buffer:
                                line, line_buffer = line_buffer.split(b'\n', 1)
                                line_str = self._decode_line(line)
                                line_callback(line_str)

                    if truncated:
                        break

                # Handle final partial line
                if line_buffer and not truncated:
                    line_callback(self._decode_line(line_buffer))

        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

        return CommandResult(
            return_code=process.returncode,
            total_bytes=total_bytes,
            truncated=truncated,
        )

    def _decode_line(self, data: bytes) -> str:
        """Decode with fallback, truncate if too long."""
        try:
            line = data.decode('utf-8')
        except UnicodeDecodeError:
            line = data.decode('latin-1')

        if len(line) > self.max_line_length:
            return line[:self.max_line_length] + '... [TRUNCATED]'
        return line
```

### 4.4 Chunked Symbol Table Processing

**Problem:** Symbol tables can have millions of entries. Storing all in a list exhausts memory.

**Solution:** Process in chunks, compute incremental statistics, optionally spill to disk.

```python
class SymbolAccumulator:
    """
    Memory-efficient symbol table accumulator.

    Strategies based on symbol count:
    - Small (< 10,000): Keep all in memory
    - Medium (10,000 - 100,000): Keep sample + statistics
    - Large (> 100,000): Statistics only + disk spill for full data
    """

    def __init__(
        self,
        memory_limit: int = 200 * 1024 * 1024,
        sample_size: int = 1000,
        spill_threshold: int = 100000,
        temp_dir: Optional[Path] = None,
    ):
        self.memory_limit = memory_limit
        self.sample_size = sample_size
        self.spill_threshold = spill_threshold
        self.temp_dir = temp_dir or Path(tempfile.gettempdir())

        # In-memory storage
        self._symbols: List[Symbol] = []
        self._sample: List[Symbol] = []

        # Statistics (always computed)
        self._stats = SymbolStats()

        # Disk spill
        self._spill_file: Optional[IO] = None
        self._spilled = False

        # Tracking
        self._count = 0
        self._estimated_memory = 0

    def add(self, symbol: Symbol) -> None:
        """Add a symbol, managing memory automatically."""
        self._count += 1
        self._stats.update(symbol)

        symbol_size = self._estimate_size(symbol)

        if not self._spilled:
            if self._estimated_memory + symbol_size > self.memory_limit:
                self._spill_to_disk()
            else:
                self._symbols.append(symbol)
                self._estimated_memory += symbol_size

        if self._spilled:
            self._write_to_spill(symbol)

        # Maintain sample using reservoir sampling
        if self._count <= self.sample_size:
            self._sample.append(symbol)
        else:
            # Reservoir sampling for uniform sample
            j = random.randint(0, self._count - 1)
            if j < self.sample_size:
                self._sample[j] = symbol

    def _spill_to_disk(self) -> None:
        """Spill current symbols to temporary file."""
        self._spill_file = tempfile.NamedTemporaryFile(
            mode='w',
            suffix='.jsonl',
            dir=self.temp_dir,
            delete=False,
        )

        for symbol in self._symbols:
            self._spill_file.write(json.dumps(symbol.to_dict()) + '\n')

        self._symbols.clear()
        self._estimated_memory = 0
        self._spilled = True

    def get_result(self) -> SymbolResult:
        """Get final result with statistics and available data."""
        return SymbolResult(
            count=self._count,
            stats=self._stats,
            sample=self._sample,
            all_symbols=self._symbols if not self._spilled else None,
            spill_file=Path(self._spill_file.name) if self._spilled else None,
        )

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.cleanup()

    def cleanup(self) -> None:
        """Clean up temporary files."""
        if self._spill_file:
            self._spill_file.close()
            try:
                os.unlink(self._spill_file.name)
            except OSError:
                pass
```

### 4.5 Comparison Memory Strategy

**Problem:** Comparing two large snapshots requires holding both in memory simultaneously.

**Solution:** Staged comparison with early termination and incremental diffing.

```python
class MemoryEfficientComparator:
    """
    Compare snapshots without loading both fully into memory.

    Strategy:
    1. Compare metadata first (fast, small)
    2. Compare section headers (small fixed size)
    3. Compare symbols incrementally using sorted merge
    4. Stream diff output instead of accumulating
    """

    def compare_symbols_streaming(
        self,
        symbols1: Iterator[Symbol],
        symbols2: Iterator[Symbol],
        diff_callback: Callable[[SymbolDiff], None],
    ) -> SymbolComparisonStats:
        """
        Compare two sorted symbol streams using merge algorithm.

        Memory usage: O(1) - only holds current symbols from each stream.
        Time complexity: O(n + m) where n, m are symbol counts.
        """
        stats = SymbolComparisonStats()

        sym1 = next(symbols1, None)
        sym2 = next(symbols2, None)

        while sym1 is not None or sym2 is not None:
            if sym1 is None:
                # sym2 is added
                diff_callback(SymbolDiff(type='added', symbol=sym2))
                stats.added += 1
                sym2 = next(symbols2, None)
            elif sym2 is None:
                # sym1 is removed
                diff_callback(SymbolDiff(type='removed', symbol=sym1))
                stats.removed += 1
                sym1 = next(symbols1, None)
            elif sym1.name < sym2.name:
                # sym1 is removed
                diff_callback(SymbolDiff(type='removed', symbol=sym1))
                stats.removed += 1
                sym1 = next(symbols1, None)
            elif sym1.name > sym2.name:
                # sym2 is added
                diff_callback(SymbolDiff(type='added', symbol=sym2))
                stats.added += 1
                sym2 = next(symbols2, None)
            else:
                # Same name - check for changes
                if sym1 != sym2:
                    diff_callback(SymbolDiff(type='modified', old=sym1, new=sym2))
                    stats.modified += 1
                else:
                    stats.unchanged += 1
                sym1 = next(symbols1, None)
                sym2 = next(symbols2, None)

        return stats
```

### 4.6 Buffer Pool for Reuse

```python
class BufferPool:
    """
    Reusable buffer pool to reduce allocation overhead.

    Pre-allocates buffers and recycles them to avoid:
    - Repeated malloc/free overhead
    - Memory fragmentation
    - GC pressure in Python
    """

    def __init__(
        self,
        buffer_size: int = 64 * 1024,
        pool_size: int = 8,
    ):
        self.buffer_size = buffer_size
        self._pool: List[bytearray] = [
            bytearray(buffer_size) for _ in range(pool_size)
        ]
        self._available = list(range(pool_size))
        self._lock = threading.Lock()

    @contextmanager
    def get_buffer(self) -> Iterator[memoryview]:
        """Get a buffer from the pool, return when done."""
        with self._lock:
            if self._available:
                idx = self._available.pop()
            else:
                # Pool exhausted, create temporary
                yield memoryview(bytearray(self.buffer_size))
                return

        try:
            yield memoryview(self._pool[idx])
        finally:
            with self._lock:
                self._available.append(idx)
```

---

## 5. Extensibility Architecture

### 5.1 Analyzer Plugin System

The core extensibility mechanism is the Analyzer abstract base class:

```python
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Optional, List, Dict, Any, Iterator
from enum import Enum

class AnalyzerCapability(Enum):
    """Capabilities that an analyzer may support."""
    SNAPSHOT = "snapshot"           # Can capture data
    COMPARE = "compare"             # Can compare two results
    STREAMING = "streaming"         # Supports streaming output
    INCREMENTAL = "incremental"     # Can process incrementally
    PARALLEL_SAFE = "parallel_safe" # Safe to run in parallel


@dataclass
class AnalyzerMetadata:
    """Metadata describing an analyzer."""
    name: str                       # Unique identifier: 'what', 'dump-h', etc.
    display_name: str               # Human-readable: 'SCCS What Strings'
    description: str                # Longer description
    version: str                    # Analyzer version
    required_commands: List[str]    # External commands needed: ['what'], ['dump']
    capabilities: List[AnalyzerCapability]
    default_enabled: bool = True    # Include in default analysis
    priority: int = 100             # Execution order (lower = earlier)


@dataclass
class AnalysisResult:
    """Result from a single analyzer."""
    analyzer_name: str
    success: bool
    data: Dict[str, Any]            # Analyzer-specific structured data
    raw_output: Optional[str]       # Raw command output (if kept)
    error: Optional[str]            # Error message if failed
    metadata: Dict[str, Any]        # Timing, byte counts, etc.
    truncated: bool = False         # Was output truncated?


@dataclass
class ComparisonResult:
    """Result from comparing two analysis results."""
    analyzer_name: str
    has_differences: bool
    summary: str                    # One-line summary
    differences: List[Dict[str, Any]]  # Structured diff data
    statistics: Dict[str, int]      # Counts: added, removed, modified


class Analyzer(ABC):
    """
    Abstract base class for all analyzers.

    To create a new analyzer:
    1. Subclass Analyzer
    2. Implement metadata property
    3. Implement analyze() method
    4. Optionally implement compare() for custom comparison logic
    5. Register with AnalyzerRegistry

    Example:
        class MyAnalyzer(Analyzer):
            @property
            def metadata(self) -> AnalyzerMetadata:
                return AnalyzerMetadata(
                    name='my-analyzer',
                    display_name='My Custom Analyzer',
                    description='Analyzes custom aspects of XCOFF',
                    version='1.0.0',
                    required_commands=['my_command'],
                    capabilities=[AnalyzerCapability.SNAPSHOT],
                )

            def analyze(self, filepath: str, options: AnalyzerOptions) -> AnalysisResult:
                # Run analysis and return result
                ...
    """

    @property
    @abstractmethod
    def metadata(self) -> AnalyzerMetadata:
        """Return metadata describing this analyzer."""
        pass

    @abstractmethod
    def analyze(
        self,
        filepath: str,
        options: 'AnalyzerOptions',
    ) -> AnalysisResult:
        """
        Analyze an XCOFF file.

        Args:
            filepath: Path to XCOFF file
            options: Analysis options (timeout, limits, etc.)

        Returns:
            AnalysisResult with structured data
        """
        pass

    def compare(
        self,
        result1: AnalysisResult,
        result2: AnalysisResult,
        options: 'CompareOptions',
    ) -> ComparisonResult:
        """
        Compare two analysis results.

        Default implementation uses generic dict comparison.
        Override for custom comparison logic.
        """
        return self._default_compare(result1, result2, options)

    def check_requirements(self) -> List[str]:
        """
        Check if required commands are available.

        Returns:
            List of missing commands (empty if all present)
        """
        missing = []
        for cmd in self.metadata.required_commands:
            if not shutil.which(cmd):
                missing.append(cmd)
        return missing

    def _default_compare(
        self,
        result1: AnalysisResult,
        result2: AnalysisResult,
        options: 'CompareOptions',
    ) -> ComparisonResult:
        """Generic comparison using deep dict diff."""
        # Implementation using deepdiff or custom logic
        ...


@dataclass
class AnalyzerOptions:
    """Options passed to analyzers."""
    timeout: float = 300.0
    max_output_size: int = 100 * 1024 * 1024
    max_line_length: int = 65536
    max_symbols: Optional[int] = None
    temp_dir: Optional[Path] = None
    verbose: bool = False


@dataclass
class CompareOptions:
    """Options for comparison operations."""
    ignore_timestamps: bool = False
    ignore_addresses: bool = False
    context_lines: int = 3
    summary_only: bool = False
```

### 5.2 Analyzer Registry

```python
class AnalyzerRegistry:
    """
    Central registry for analyzer discovery and management.

    Features:
    - Auto-discovery of built-in analyzers
    - Plugin loading from external modules
    - Dependency ordering
    - Capability filtering
    """

    _instance: Optional['AnalyzerRegistry'] = None

    def __init__(self):
        self._analyzers: Dict[str, Type[Analyzer]] = {}
        self._instances: Dict[str, Analyzer] = {}

    @classmethod
    def get_instance(cls) -> 'AnalyzerRegistry':
        """Get singleton registry instance."""
        if cls._instance is None:
            cls._instance = cls()
            cls._instance._load_builtin_analyzers()
        return cls._instance

    def register(
        self,
        analyzer_class: Type[Analyzer],
        replace: bool = False,
    ) -> None:
        """
        Register an analyzer class.

        Args:
            analyzer_class: Analyzer subclass to register
            replace: If True, replace existing analyzer with same name
        """
        # Create instance to get metadata
        instance = analyzer_class()
        name = instance.metadata.name

        if name in self._analyzers and not replace:
            raise ValueError(f"Analyzer '{name}' already registered")

        self._analyzers[name] = analyzer_class
        self._instances[name] = instance

    def get(self, name: str) -> Analyzer:
        """Get analyzer instance by name."""
        if name not in self._instances:
            raise KeyError(f"Unknown analyzer: {name}")
        return self._instances[name]

    def get_all(
        self,
        enabled_only: bool = True,
        capability: Optional[AnalyzerCapability] = None,
    ) -> List[Analyzer]:
        """
        Get all matching analyzers, ordered by priority.

        Args:
            enabled_only: Only return default-enabled analyzers
            capability: Filter by required capability
        """
        analyzers = list(self._instances.values())

        if enabled_only:
            analyzers = [a for a in analyzers if a.metadata.default_enabled]

        if capability:
            analyzers = [
                a for a in analyzers
                if capability in a.metadata.capabilities
            ]

        return sorted(analyzers, key=lambda a: a.metadata.priority)

    def get_by_names(self, names: List[str]) -> List[Analyzer]:
        """Get specific analyzers by name, preserving order."""
        return [self.get(name) for name in names]

    def list_available(self) -> List[AnalyzerMetadata]:
        """List metadata for all registered analyzers."""
        return [a.metadata for a in self._instances.values()]

    def load_plugin(self, module_path: str) -> None:
        """
        Load analyzer plugin from external module.

        The module should have a `register_analyzers(registry)` function
        or classes decorated with @analyzer.
        """
        import importlib.util
        spec = importlib.util.spec_from_file_location("plugin", module_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        if hasattr(module, 'register_analyzers'):
            module.register_analyzers(self)

        # Also look for @analyzer decorated classes
        for name, obj in vars(module).items():
            if isinstance(obj, type) and issubclass(obj, Analyzer) and obj is not Analyzer:
                self.register(obj)

    def _load_builtin_analyzers(self) -> None:
        """Load all built-in analyzers."""
        from .analyzers import (
            WhatAnalyzer,
            DumpHeadersAnalyzer,
            DumpLoaderAnalyzer,
            DumpSymbolsAnalyzer,
        )

        for cls in [
            WhatAnalyzer,
            DumpHeadersAnalyzer,
            DumpLoaderAnalyzer,
            DumpSymbolsAnalyzer,
        ]:
            self.register(cls)


def analyzer(cls: Type[Analyzer]) -> Type[Analyzer]:
    """
    Decorator to auto-register an analyzer.

    Usage:
        @analyzer
        class MyAnalyzer(Analyzer):
            ...
    """
    AnalyzerRegistry.get_instance().register(cls)
    return cls
```

### 5.3 Built-in Analyzer Example: WhatAnalyzer

```python
class WhatAnalyzer(Analyzer):
    """
    Analyzer using the 'what' command to extract SCCS identification strings.

    The 'what' command searches for @(#) patterns and extracts embedded
    version information, build timestamps, and source file identifiers.
    """

    @property
    def metadata(self) -> AnalyzerMetadata:
        return AnalyzerMetadata(
            name='what',
            display_name='SCCS What Strings',
            description='Extract @(#) identification strings from binary',
            version='1.0.0',
            required_commands=['what'],
            capabilities=[
                AnalyzerCapability.SNAPSHOT,
                AnalyzerCapability.COMPARE,
                AnalyzerCapability.PARALLEL_SAFE,
            ],
            default_enabled=True,
            priority=10,  # Run early - fast and informative
        )

    def analyze(
        self,
        filepath: str,
        options: AnalyzerOptions,
    ) -> AnalysisResult:
        """Run 'what' command and parse output."""

        runner = StreamingCommandRunner(
            timeout=options.timeout,
            max_total_output=options.max_output_size,
        )

        strings: List[str] = []
        parsed: List[Dict[str, Any]] = []

        def process_line(line: str) -> None:
            line = line.strip()
            if line and not line.endswith(':'):  # Skip filename header
                strings.append(line)
                parsed.append(self._parse_what_string(line))

        try:
            result = runner.run_streaming(
                ['what', filepath],
                process_line,
            )

            return AnalysisResult(
                analyzer_name=self.metadata.name,
                success=result.return_code == 0,
                data={
                    'strings': strings,
                    'parsed': parsed,
                    'count': len(strings),
                },
                raw_output=None,  # Don't keep raw - we have parsed
                error=None,
                metadata={
                    'return_code': result.return_code,
                    'total_bytes': result.total_bytes,
                    'truncated': result.truncated,
                },
            )

        except TimeoutError as e:
            return AnalysisResult(
                analyzer_name=self.metadata.name,
                success=False,
                data={},
                raw_output=None,
                error=str(e),
                metadata={},
            )

    def _parse_what_string(self, s: str) -> Dict[str, Any]:
        """
        Parse a what string into structured data.

        Common formats:
        - "filename.c 1.5 2024/01/15 10:30:00"
        - "module.c,v 1.23 2024/01/15 10:30:00 user Exp"
        - Custom: "@(#)MyApp Version 2.1.0 Built: Jan 15 2024"
        """
        result = {'raw': s}

        # Try to extract common patterns
        patterns = [
            # RCS/SCCS style: filename version date time
            (r'^(\S+)\s+(\d+\.\d+)\s+(\d{4}/\d{2}/\d{2})\s+(\d{2}:\d{2}:\d{2})',
             ['filename', 'version', 'date', 'time']),

            # Version keyword style: "Version X.Y.Z"
            (r'[Vv]ersion\s+(\d+\.\d+(?:\.\d+)?)',
             ['version']),

            # Built timestamp: "Built: Mon DD YYYY"
            (r'[Bb]uilt[:\s]+(\w+\s+\d+\s+\d{4})',
             ['build_date']),
        ]

        for pattern, fields in patterns:
            match = re.search(pattern, s)
            if match:
                for i, field in enumerate(fields):
                    if i < len(match.groups()):
                        result[field] = match.group(i + 1)

        return result

    def compare(
        self,
        result1: AnalysisResult,
        result2: AnalysisResult,
        options: CompareOptions,
    ) -> ComparisonResult:
        """Compare what strings between two binaries."""

        strings1 = set(result1.data.get('strings', []))
        strings2 = set(result2.data.get('strings', []))

        added = strings2 - strings1
        removed = strings1 - strings2
        unchanged = strings1 & strings2

        differences = []

        for s in removed:
            differences.append({'type': 'removed', 'string': s})
        for s in added:
            differences.append({'type': 'added', 'string': s})

        # Check for version changes in unchanged strings
        # (same file, different version)
        # ... implementation ...

        return ComparisonResult(
            analyzer_name=self.metadata.name,
            has_differences=len(added) > 0 or len(removed) > 0,
            summary=f"what: +{len(added)} -{len(removed)} ={len(unchanged)}",
            differences=differences,
            statistics={
                'added': len(added),
                'removed': len(removed),
                'unchanged': len(unchanged),
            },
        )
```

### 5.4 Adding a New Analyzer

To add a new analyzer (e.g., for `ar -t` to list archive members):

```python
# File: analyzers/ar_contents.py

from .base import Analyzer, AnalyzerMetadata, AnalyzerCapability, AnalysisResult

class ArContentsAnalyzer(Analyzer):
    """
    Analyzer for archive (.a) files using 'ar -t'.

    Lists member objects within an archive library.
    """

    @property
    def metadata(self) -> AnalyzerMetadata:
        return AnalyzerMetadata(
            name='ar-contents',
            display_name='Archive Contents',
            description='List member objects in .a archive files',
            version='1.0.0',
            required_commands=['ar'],
            capabilities=[
                AnalyzerCapability.SNAPSHOT,
                AnalyzerCapability.COMPARE,
                AnalyzerCapability.PARALLEL_SAFE,
            ],
            default_enabled=True,
            priority=50,
        )

    def analyze(
        self,
        filepath: str,
        options: AnalyzerOptions,
    ) -> AnalysisResult:
        # Check if file is an archive
        if not self._is_archive(filepath):
            return AnalysisResult(
                analyzer_name=self.metadata.name,
                success=True,
                data={'members': [], 'is_archive': False},
                raw_output=None,
                error=None,
                metadata={'skipped': 'not an archive'},
            )

        members = []

        def process_line(line: str) -> None:
            line = line.strip()
            if line:
                members.append(line)

        runner = StreamingCommandRunner(timeout=options.timeout)
        result = runner.run_streaming(['ar', '-t', filepath], process_line)

        return AnalysisResult(
            analyzer_name=self.metadata.name,
            success=result.return_code == 0,
            data={
                'members': members,
                'member_count': len(members),
                'is_archive': True,
            },
            raw_output=None,
            error=None,
            metadata={'return_code': result.return_code},
        )

    def _is_archive(self, filepath: str) -> bool:
        """Check if file is an ar archive."""
        try:
            with open(filepath, 'rb') as f:
                magic = f.read(8)
                return magic == b'!<arch>\n'
        except IOError:
            return False


# Register the analyzer
from ..core.registry import analyzer

@analyzer
class ArContentsAnalyzer(ArContentsAnalyzer):
    pass
```

### 5.5 Plugin Configuration

```yaml
# ~/.xcoff_tool/plugins.yaml

plugins:
  - path: /usr/local/lib/xcoff_plugins/custom_analyzer.py
    enabled: true

  - path: ~/my_analyzers/special.py
    enabled: true
    config:
      custom_option: value

disabled_analyzers:
  - dump-symbols  # Too slow for my workflow
```

---

## 6. Error Handling and Robustness

### 6.1 Exception Hierarchy

```python
class XCOFFToolError(Exception):
    """Base exception for all tool errors."""
    pass


class FileError(XCOFFToolError):
    """File-related errors."""
    pass

class FileNotFoundError(FileError):
    pass

class InvalidXCOFFError(FileError):
    """File is not a valid XCOFF object."""
    pass

class FileAccessError(FileError):
    """Cannot read file (permissions, etc.)."""
    pass


class AnalyzerError(XCOFFToolError):
    """Analyzer execution errors."""
    pass

class CommandNotFoundError(AnalyzerError):
    """Required external command not found."""
    pass

class CommandTimeoutError(AnalyzerError):
    """Command execution timed out."""
    pass

class CommandFailedError(AnalyzerError):
    """Command returned non-zero exit code."""
    pass

class OutputTruncatedError(AnalyzerError):
    """Output exceeded maximum size and was truncated."""
    pass


class MemoryError(XCOFFToolError):
    """Memory-related errors."""
    pass

class MemoryLimitExceeded(MemoryError):
    """Operation would exceed memory budget."""
    pass


class ConfigError(XCOFFToolError):
    """Configuration errors."""
    pass
```

### 6.2 XCOFF File Validation

```python
class XCOFFValidator:
    """
    Validate that a file is a legitimate XCOFF object.

    Checks:
    1. File exists and is readable
    2. Magic number is valid XCOFF
    3. Header structure is consistent
    4. File size matches header claims
    """

    # XCOFF magic numbers
    MAGIC_32 = 0x01DF  # 32-bit XCOFF
    MAGIC_64 = 0x01F7  # 64-bit XCOFF

    # Archive magic
    AR_MAGIC = b'!<arch>\n'

    @dataclass
    class ValidationResult:
        valid: bool
        file_type: Optional[str]  # 'xcoff32', 'xcoff64', 'archive', None
        error: Optional[str]
        details: Dict[str, Any]

    def validate(self, filepath: str) -> ValidationResult:
        """Validate file and return detailed result."""

        # Check existence
        path = Path(filepath)
        if not path.exists():
            return self.ValidationResult(
                valid=False,
                file_type=None,
                error=f"File not found: {filepath}",
                details={},
            )

        # Check readable
        if not os.access(filepath, os.R_OK):
            return self.ValidationResult(
                valid=False,
                file_type=None,
                error=f"File not readable: {filepath}",
                details={},
            )

        # Check size
        file_size = path.stat().st_size
        if file_size < 20:  # Minimum XCOFF header size
            return self.ValidationResult(
                valid=False,
                file_type=None,
                error=f"File too small to be XCOFF: {file_size} bytes",
                details={'file_size': file_size},
            )

        # Read and check magic
        try:
            with open(filepath, 'rb') as f:
                header = f.read(20)

                # Check for archive
                if header[:8] == self.AR_MAGIC:
                    return self.ValidationResult(
                        valid=True,
                        file_type='archive',
                        error=None,
                        details={'file_size': file_size},
                    )

                # Check XCOFF magic (big-endian)
                magic = struct.unpack('>H', header[:2])[0]

                if magic == self.MAGIC_32:
                    return self._validate_xcoff32(f, header, file_size)
                elif magic == self.MAGIC_64:
                    return self._validate_xcoff64(f, header, file_size)
                else:
                    return self.ValidationResult(
                        valid=False,
                        file_type=None,
                        error=f"Invalid magic number: 0x{magic:04X}",
                        details={'magic': magic, 'file_size': file_size},
                    )

        except IOError as e:
            return self.ValidationResult(
                valid=False,
                file_type=None,
                error=f"I/O error reading file: {e}",
                details={},
            )

    def _validate_xcoff32(
        self,
        f: BinaryIO,
        header: bytes,
        file_size: int,
    ) -> ValidationResult:
        """Validate 32-bit XCOFF structure."""
        # Parse file header
        # struct filehdr (20 bytes):
        #   unsigned short f_magic;    # Magic number
        #   unsigned short f_nscns;    # Number of sections
        #   int f_timdat;              # Timestamp
        #   int f_symptr;              # Symbol table offset
        #   int f_nsyms;               # Number of symbols
        #   unsigned short f_opthdr;   # Optional header size
        #   unsigned short f_flags;    # Flags

        magic, nscns, timdat, symptr, nsyms, opthdr, flags = struct.unpack(
            '>HHIIIHH', header
        )

        details = {
            'magic': f'0x{magic:04X}',
            'sections': nscns,
            'timestamp': timdat,
            'symbol_table_offset': symptr,
            'symbol_count': nsyms,
            'optional_header_size': opthdr,
            'flags': f'0x{flags:04X}',
            'file_size': file_size,
        }

        # Sanity checks
        errors = []

        if nscns > 1000:  # Unreasonable section count
            errors.append(f"Suspicious section count: {nscns}")

        if symptr > 0 and symptr > file_size:
            errors.append(f"Symbol table offset ({symptr}) beyond file size ({file_size})")

        if opthdr > 0 and opthdr > 1024:  # Unreasonably large optional header
            errors.append(f"Suspicious optional header size: {opthdr}")

        if errors:
            return self.ValidationResult(
                valid=False,
                file_type='xcoff32',
                error='; '.join(errors),
                details=details,
            )

        return self.ValidationResult(
            valid=True,
            file_type='xcoff32',
            error=None,
            details=details,
        )
```

### 6.3 Graceful Degradation

```python
class RobustAnalysisEngine:
    """
    Engine that continues analysis even when individual analyzers fail.
    """

    def analyze_file(
        self,
        filepath: str,
        analyzers: List[Analyzer],
        options: AnalyzerOptions,
        fail_fast: bool = False,
    ) -> SnapshotResult:
        """
        Run all analyzers, collecting results and errors.

        Args:
            filepath: Path to XCOFF file
            analyzers: List of analyzers to run
            options: Analysis options
            fail_fast: If True, stop on first failure

        Returns:
            SnapshotResult containing all results and any errors
        """
        results: Dict[str, AnalysisResult] = {}
        errors: List[Tuple[str, Exception]] = []

        for analyzer in analyzers:
            try:
                # Check requirements first
                missing = analyzer.check_requirements()
                if missing:
                    results[analyzer.metadata.name] = AnalysisResult(
                        analyzer_name=analyzer.metadata.name,
                        success=False,
                        data={},
                        raw_output=None,
                        error=f"Missing commands: {', '.join(missing)}",
                        metadata={'skipped': True},
                    )
                    continue

                # Run analysis
                result = analyzer.analyze(filepath, options)
                results[analyzer.metadata.name] = result

                if not result.success and fail_fast:
                    break

            except Exception as e:
                errors.append((analyzer.metadata.name, e))

                results[analyzer.metadata.name] = AnalysisResult(
                    analyzer_name=analyzer.metadata.name,
                    success=False,
                    data={},
                    raw_output=None,
                    error=f"{type(e).__name__}: {e}",
                    metadata={'exception': True},
                )

                if fail_fast:
                    break

        return SnapshotResult(
            filepath=filepath,
            results=results,
            errors=errors,
            complete=len(errors) == 0,
            timestamp=datetime.now(),
        )
```

### 6.4 Signal Handling

```python
class SignalHandler:
    """
    Handle signals gracefully for clean shutdown.
    """

    def __init__(self):
        self._shutdown_requested = False
        self._original_handlers: Dict[int, Any] = {}
        self._cleanup_callbacks: List[Callable[[], None]] = []

    def install(self) -> None:
        """Install signal handlers."""
        for sig in (signal.SIGINT, signal.SIGTERM):
            self._original_handlers[sig] = signal.signal(sig, self._handler)

    def restore(self) -> None:
        """Restore original signal handlers."""
        for sig, handler in self._original_handlers.items():
            signal.signal(sig, handler)
        self._original_handlers.clear()

    def register_cleanup(self, callback: Callable[[], None]) -> None:
        """Register a cleanup callback to run on shutdown."""
        self._cleanup_callbacks.append(callback)

    @property
    def shutdown_requested(self) -> bool:
        """Check if shutdown has been requested."""
        return self._shutdown_requested

    def _handler(self, signum: int, frame: Any) -> None:
        """Handle signal by requesting shutdown."""
        if self._shutdown_requested:
            # Second signal - force exit
            sys.exit(128 + signum)

        self._shutdown_requested = True
        print("\nShutdown requested, cleaning up...", file=sys.stderr)

        for callback in self._cleanup_callbacks:
            try:
                callback()
            except Exception:
                pass  # Best effort cleanup
```

---

## 7. Performance Optimization

### 7.1 Parallel Analyzer Execution

```python
class ParallelAnalyzerRunner:
    """
    Run multiple analyzers in parallel for faster analysis.

    Only analyzers marked PARALLEL_SAFE are run concurrently.
    Others run sequentially after parallel batch.
    """

    def __init__(self, max_workers: int = 4):
        self.max_workers = max_workers

    def run_all(
        self,
        filepath: str,
        analyzers: List[Analyzer],
        options: AnalyzerOptions,
    ) -> Dict[str, AnalysisResult]:
        """Run analyzers with parallelism where safe."""

        # Separate parallel-safe from sequential
        parallel = []
        sequential = []

        for analyzer in analyzers:
            if AnalyzerCapability.PARALLEL_SAFE in analyzer.metadata.capabilities:
                parallel.append(analyzer)
            else:
                sequential.append(analyzer)

        results: Dict[str, AnalysisResult] = {}

        # Run parallel analyzers
        if parallel:
            with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
                futures = {
                    executor.submit(a.analyze, filepath, options): a
                    for a in parallel
                }

                for future in as_completed(futures):
                    analyzer = futures[future]
                    try:
                        result = future.result()
                        results[analyzer.metadata.name] = result
                    except Exception as e:
                        results[analyzer.metadata.name] = AnalysisResult(
                            analyzer_name=analyzer.metadata.name,
                            success=False,
                            data={},
                            raw_output=None,
                            error=str(e),
                            metadata={},
                        )

        # Run sequential analyzers
        for analyzer in sequential:
            try:
                result = analyzer.analyze(filepath, options)
                results[analyzer.metadata.name] = result
            except Exception as e:
                results[analyzer.metadata.name] = AnalysisResult(
                    analyzer_name=analyzer.metadata.name,
                    success=False,
                    data={},
                    raw_output=None,
                    error=str(e),
                    metadata={},
                )

        return results
```

### 7.2 Caching Strategy

```python
class AnalysisCache:
    """
    Cache analysis results to avoid re-running on unchanged files.

    Cache key: (filepath, mtime, size, analyzer_version)
    """

    def __init__(
        self,
        cache_dir: Optional[Path] = None,
        max_size_mb: int = 500,
        ttl_hours: int = 24,
    ):
        self.cache_dir = cache_dir or Path.home() / '.cache' / 'xcoff_tool'
        self.max_size_mb = max_size_mb
        self.ttl_hours = ttl_hours
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def _cache_key(
        self,
        filepath: str,
        analyzer_name: str,
        analyzer_version: str,
    ) -> str:
        """Generate cache key from file metadata."""
        stat = os.stat(filepath)
        key_data = f"{filepath}:{stat.st_mtime}:{stat.st_size}:{analyzer_name}:{analyzer_version}"
        return hashlib.sha256(key_data.encode()).hexdigest()[:32]

    def get(
        self,
        filepath: str,
        analyzer: Analyzer,
    ) -> Optional[AnalysisResult]:
        """Get cached result if available and valid."""
        key = self._cache_key(
            filepath,
            analyzer.metadata.name,
            analyzer.metadata.version,
        )
        cache_file = self.cache_dir / f"{key}.json"

        if not cache_file.exists():
            return None

        # Check TTL
        age_hours = (time.time() - cache_file.stat().st_mtime) / 3600
        if age_hours > self.ttl_hours:
            cache_file.unlink()
            return None

        try:
            with open(cache_file, 'r') as f:
                data = json.load(f)
                return AnalysisResult(**data)
        except (json.JSONDecodeError, KeyError):
            cache_file.unlink()
            return None

    def put(
        self,
        filepath: str,
        analyzer: Analyzer,
        result: AnalysisResult,
    ) -> None:
        """Store result in cache."""
        key = self._cache_key(
            filepath,
            analyzer.metadata.name,
            analyzer.metadata.version,
        )
        cache_file = self.cache_dir / f"{key}.json"

        # Enforce cache size limit
        self._enforce_size_limit()

        with open(cache_file, 'w') as f:
            json.dump(result.__dict__, f)

    def _enforce_size_limit(self) -> None:
        """Remove old entries if cache exceeds size limit."""
        total_size = sum(f.stat().st_size for f in self.cache_dir.glob('*.json'))
        max_bytes = self.max_size_mb * 1024 * 1024

        if total_size > max_bytes:
            # Remove oldest files until under limit
            files = sorted(
                self.cache_dir.glob('*.json'),
                key=lambda f: f.stat().st_mtime,
            )

            for f in files:
                if total_size <= max_bytes * 0.8:  # Target 80%
                    break
                total_size -= f.stat().st_size
                f.unlink()
```

---

## 8. Output Formatting

### 8.1 Format Registry

```python
class OutputFormat(Enum):
    TEXT = "text"
    JSON = "json"
    JSONL = "jsonl"  # JSON Lines for streaming
    CSV = "csv"


class OutputFormatter(ABC):
    """Base class for output formatters."""

    @abstractmethod
    def format_snapshot(self, snapshot: SnapshotResult) -> str:
        pass

    @abstractmethod
    def format_comparison(self, comparison: FullComparisonResult) -> str:
        pass

    @abstractmethod
    def format_info(self, info: FileInfo) -> str:
        pass


class TextFormatter(OutputFormatter):
    """Human-readable text output."""

    def __init__(self, color: bool = True, verbose: bool = False):
        self.color = color
        self.verbose = verbose

    def format_snapshot(self, snapshot: SnapshotResult) -> str:
        lines = []
        lines.append(f"XCOFF Analysis: {snapshot.filepath}")
        lines.append(f"Timestamp: {snapshot.timestamp}")
        lines.append("=" * 60)

        for name, result in snapshot.results.items():
            lines.append(f"\n[{name}]")
            if result.success:
                lines.extend(self._format_result_data(result.data))
            else:
                lines.append(f"  ERROR: {result.error}")

        return '\n'.join(lines)

    # ... more formatting methods


class JSONFormatter(OutputFormatter):
    """JSON output for programmatic consumption."""

    def __init__(self, indent: int = 2):
        self.indent = indent

    def format_snapshot(self, snapshot: SnapshotResult) -> str:
        return json.dumps(
            snapshot.to_dict(),
            indent=self.indent,
            default=str,
        )
```

---

## 9. Configuration

### 9.1 Configuration File

```yaml
# ~/.xcoff_tool/config.yaml

# Global settings
global:
  default_format: text
  color: auto  # auto, always, never
  verbose: false

# Memory limits
memory:
  per_analyzer_output_mb: 100
  symbol_accumulator_mb: 200
  comparison_working_mb: 100

# Timeout settings
timeouts:
  default_seconds: 300
  dump_symbols_seconds: 600  # Longer for symbol tables

# Cache settings
cache:
  enabled: true
  directory: ~/.cache/xcoff_tool
  max_size_mb: 500
  ttl_hours: 24

# Analyzer settings
analyzers:
  what:
    enabled: true
  dump-h:
    enabled: true
  dump-T:
    enabled: true
  dump-t:
    enabled: false  # Disabled by default - slow

# Comparison settings
comparison:
  ignore_timestamps: false
  ignore_addresses: false
  context_lines: 3

# Plugin paths
plugins:
  - ~/.xcoff_tool/plugins/
```

### 9.2 Configuration Loading

```python
@dataclass
class Config:
    """Tool configuration."""

    # Global
    default_format: OutputFormat = OutputFormat.TEXT
    color: str = 'auto'
    verbose: bool = False

    # Memory
    per_analyzer_output_mb: int = 100
    symbol_accumulator_mb: int = 200
    comparison_working_mb: int = 100

    # Timeouts
    default_timeout: float = 300.0
    analyzer_timeouts: Dict[str, float] = field(default_factory=dict)

    # Cache
    cache_enabled: bool = True
    cache_directory: Path = field(default_factory=lambda: Path.home() / '.cache' / 'xcoff_tool')
    cache_max_size_mb: int = 500
    cache_ttl_hours: int = 24

    # Analyzers
    disabled_analyzers: List[str] = field(default_factory=list)

    # Comparison
    ignore_timestamps: bool = False
    ignore_addresses: bool = False
    context_lines: int = 3

    # Plugins
    plugin_paths: List[Path] = field(default_factory=list)

    @classmethod
    def load(cls, config_path: Optional[Path] = None) -> 'Config':
        """Load configuration from file."""
        if config_path is None:
            config_path = Path.home() / '.xcoff_tool' / 'config.yaml'

        if not config_path.exists():
            return cls()  # Default config

        with open(config_path, 'r') as f:
            data = yaml.safe_load(f)

        return cls._from_dict(data)
```

---

## 10. Data Storage

### 10.1 Storage Location

Default: `~/.xcoffscandb/`

Override via:
- `XCOFF_DB_PATH` environment variable
- `--db-path` CLI flag

### 10.2 Directory Structure

```
~/.xcoffscandb/
├── snapshots/
│   └── <name>/                 # Named snapshot set (e.g., aix73, ibmi75, baseline)
│       └── usr/
│           └── bin/
│               └── ls.json
│
└── registry.txt                # Index of named snapshots
```

### 10.3 Snapshot Names

Each stored snapshot set has a name:
- **OS release**: `aix72`, `aix73`, `ibmi74`, `ibmi75`
- **Custom**: `baseline`, `before-update`, `prod-2024-01`

### 10.4 Snapshot JSON Format

```json
{
  "version": "1.0",
  "name": "aix73",
  "filepath": "/usr/bin/ls",
  "timestamp": "2024-01-15T10:30:00",
  "file_size": 123456,
  "file_mtime": "2024-01-10T12:00:00",
  "results": {
    "what": { "success": true, "data": { "strings": [...] } },
    "dump-h": { "success": true, "data": { "sections": [...] } },
    "dump-T": { "success": true, "data": { "imports": [...], "exports": [...] } }
  }
}
```

### 10.5 Registry File (`registry.txt`)

```
# XCOFF Scan Registry
# NAME<TAB>CREATED<TAB>FILE_COUNT<TAB>DESCRIPTION
aix73	2024-01-15T10:30:00	156	AIX 7.3 TL1 baseline
ibmi75	2024-01-20T08:00:00	89	IBM i 7.5 TR3
baseline	2024-02-01T12:00:00	42	Pre-upgrade snapshot
```

### 10.6 CLI Usage

```bash
# Store snapshot with name
xcoff snapshot --store aix73 /usr/bin/ls
xcoff snapshot --store aix73 /usr/bin/*      # Multiple files

# Compare file against named snapshot
xcoff compare --against aix73 /usr/bin/ls

# Compare two named snapshots
xcoff diff aix72 aix73 /usr/bin/ls

# List named snapshots
xcoff list --stored

# List files in a named snapshot
xcoff list --stored aix73
```

---

## 12. Testing Strategy

### 12.1 Test Categories

```
tests/
├── unit/
│   ├── test_validators.py      # XCOFF validation
│   ├── test_streaming.py       # Streaming command runner
│   ├── test_memory.py          # Buffer management
│   ├── test_parsers.py         # Output parsing
│   └── test_comparison.py      # Diff algorithms
│
├── integration/
│   ├── test_analyzers.py       # Full analyzer tests
│   ├── test_cli.py             # CLI end-to-end
│   └── test_plugins.py         # Plugin loading
│
├── performance/
│   ├── test_large_files.py     # Memory usage with large files
│   ├── test_many_symbols.py    # Symbol table handling
│   └── test_parallel.py        # Parallel execution
│
└── fixtures/
    ├── small_exe              # Simple executable
    ├── large_lib.a            # Large library
    ├── many_symbols           # Object with many symbols
    └── malformed/             # Invalid XCOFF files
```

### 12.2 Memory Testing

```python
class MemoryTracker:
    """Track memory usage during tests."""

    def __init__(self, limit_mb: int = 512):
        self.limit_mb = limit_mb
        self.peak_mb = 0
        self._process = psutil.Process()

    def check(self) -> None:
        """Check current memory usage."""
        current_mb = self._process.memory_info().rss / (1024 * 1024)
        self.peak_mb = max(self.peak_mb, current_mb)

        if current_mb > self.limit_mb:
            raise MemoryLimitExceeded(
                f"Memory usage ({current_mb:.1f}MB) exceeded limit ({self.limit_mb}MB)"
            )

    @contextmanager
    def monitor(self, interval: float = 0.1):
        """Context manager to monitor memory during operation."""
        stop_event = threading.Event()

        def monitor_thread():
            while not stop_event.is_set():
                self.check()
                time.sleep(interval)

        thread = threading.Thread(target=monitor_thread, daemon=True)
        thread.start()

        try:
            yield self
        finally:
            stop_event.set()
            thread.join(timeout=1.0)


# Usage in tests
def test_large_file_memory():
    """Ensure memory stays bounded with large files."""
    tracker = MemoryTracker(limit_mb=300)

    with tracker.monitor():
        engine = AnalysisEngine()
        result = engine.analyze('fixtures/large_lib.a')

    assert tracker.peak_mb < 300, f"Peak memory: {tracker.peak_mb}MB"
```

---

## 13. Usage Examples

### 13.1 Basic Workflow

```bash
# Quick info about a file
xcoff info /usr/bin/ls

# Full snapshot with all analyzers
xcoff snapshot /usr/bin/ls

# Save snapshot for later comparison
xcoff snapshot --save before.json /usr/bin/myapp

# After PTF application
xcoff snapshot --save after.json /usr/bin/myapp

# Compare snapshots
xcoff diff before.json after.json

# Direct comparison of two files
xcoff compare /backup/myapp /usr/bin/myapp
```

### 13.2 CI/CD Integration

```bash
#!/bin/bash
# PTF validation script

set -e

# Capture pre-PTF state
xcoff snapshot --save /tmp/pre_ptf.json /usr/bin/critical_app

# Apply PTF (handled elsewhere)
# ...

# Capture post-PTF state
xcoff snapshot --save /tmp/post_ptf.json /usr/bin/critical_app

# Compare with CI-friendly output
xcoff diff --fail-on-diff --format json \
    /tmp/pre_ptf.json /tmp/post_ptf.json > /tmp/ptf_changes.json

# Parse results
if [ $? -ne 0 ]; then
    echo "PTF changed binary - review /tmp/ptf_changes.json"
    exit 1
fi
```

### 13.3 Scripted Analysis

```python
#!/usr/bin/env python3
"""Analyze all libraries in a directory."""

from xcoff_tool import SnapshotManager, Config

config = Config.load()
manager = SnapshotManager(config)

import glob
for lib in glob.glob('/usr/lib/*.a'):
    result = manager.analyze(lib)

    if not result.complete:
        print(f"WARN: {lib} had errors")
        for name, err in result.errors:
            print(f"  {name}: {err}")

    # Extract what strings
    what_data = result.results.get('what', {}).data
    for s in what_data.get('strings', []):
        if 'Version' in s:
            print(f"{lib}: {s}")
```

---

## 14. Future Extensions

### 14.1 Potential Additional Analyzers

| Analyzer | Command | Purpose |
|----------|---------|---------|
| `dump-symbols` | `dump -t` | Full symbol table |
| `dump-reloc` | `dump -r` | Relocation entries |
| `dump-line` | `dump -l` | Line number info |
| `strings` | `strings` | All printable strings |
| `nm` | `nm` | Symbol listing (alternative) |
| `ar-contents` | `ar -t` | Archive member listing |
| `size` | `size` | Section sizes summary |
| `checksums` | `cksum` | File integrity |

### 14.2 Advanced Features

- **Watch mode**: Monitor files for changes
- **Batch mode**: Analyze multiple files with summary report
- **Web interface**: Optional REST API for integration
- **Report generation**: HTML/PDF reports with graphs
- **Database backend**: Store historical analysis for trending

---

## 15. Implementation Priority

### Phase 1: Core (MVP)
1. CLI framework with argparse
2. XCOFF validation
3. `what` analyzer
4. `dump -h` analyzer
5. `dump -T` analyzer
6. Text output formatter
7. Basic comparison

### Phase 2: Robustness
1. Streaming command runner
2. Memory limits enforcement
3. Timeout handling
4. Signal handling
5. JSON output formatter

### Phase 3: Performance
1. Parallel analyzer execution
2. Caching layer
3. Large file testing

### Phase 4: Data Storage
1. Named snapshot storage (`~/.xcoffscandb/<name>/`)
2. Simple registry file
3. `--store <name>` option for snapshot command
4. `--against <name>` option for compare command
5. `xcoff list --stored` to list snapshots

### Phase 5: Extensibility
1. Plugin system
2. Configuration file support
3. Additional analyzers

---

*Document Version: 1.1*
*Last Updated: 2024*
