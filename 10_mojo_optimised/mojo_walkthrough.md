# Mojo 1BRC Implementation Walkthrough

This document explains the Mojo implementation in [10_mojo_optimised/perf.mojo](perf.mojo) section by section. The code is designed to process a very large weather dataset efficiently by:

- mapping the file into memory with `mmap`
- splitting work across CPU workers
- hashing station names and tracking aggregates per station
- merging worker-local tables into one final result
- sorting the station names and printing the final output

The file is structured in the same order as the program itself, so this walkthrough follows the exact logic of the implementation.

---

## 1. Imports and compile-time constants

```mojo
from std.collections import List
from std.ffi import external_call, c_int, c_size_t
from std.memory import Pointer
from std.origin import MutUntrackedOrigin
from std.pathlib import Path
from std.runtime.asyncrt import TaskGroup, parallelism_level


comptime TABLE_SIZE = 32768
comptime TABLE_MASK = TABLE_SIZE - 1

comptime O_RDONLY = 0
comptime PROT_READ = 0x01
comptime MAP_PRIVATE = 0x02

comptime FNV_OFFSET = UInt64(14695981039346656037)
comptime FNV_PRIME = UInt64(1099511628211)
```

### What this does

This section imports the small set of modules needed for:

- dynamic containers like `List`
- FFI calls to OS functions such as `open`, `close`, and `mmap`
- pointer and memory manipulation
- filesystem checks via `Path`
- parallel worker tasks and CPU detection

The constants establish the program’s operating model:

- `TABLE_SIZE = 32768`: each worker gets a hash table of this size
- `TABLE_MASK = TABLE_SIZE - 1`: used for fast modulo-style wraparound in the hash table
- `O_RDONLY`, `PROT_READ`, `MAP_PRIVATE`: flags for file opening and memory mapping
- `FNV_OFFSET` and `FNV_PRIME`: parameters for the FNV-1a hash used to hash station names

The FNV hash is important because it lets the program detect whether a station matches a previous one without repeatedly comparing strings byte-by-byte until a collision is ruled out.

---

## 2. The record layout for each station

```mojo
comptime Entry = Tuple[
    Bool,
    UInt64,
    Int,
    Int,
    Int,
    Int,
    Int,
    Int,
]
```

### What this does

Each entry in the hash table stores a single station’s aggregate state. The tuple structure is:

1. `Bool` — whether this slot is occupied
2. `UInt64` — the FNV hash for the station name
3. `Int` — start offset of the station name in the file buffer
4. `Int` — station name length
5. `Int` — number of observations (`count`)
6. `Int` — sum of temperatures (`total`)
7. `Int` — minimum temperature
8. `Int` — maximum temperature

This is a compact representation that allows each station to be tracked as a single record in a hash table rather than storing full strings repeatedly.

---

## 3. Comparing station names and finding line boundaries

```mojo
@always_inline
def station_equal(
    data: Pointer[UInt8, MutUntrackedOrigin],
    left: Int,
    right: Int,
    length: Int,
) -> Bool:
    var i = 0

    while i < length:
        if (
            data[unsafe_offset=left + i]
            != data[unsafe_offset=right + i]
        ):
            return False

        i += 1

    return True


def next_line(
    data: Pointer[UInt8, MutUntrackedOrigin],
    pos: Int,
    size: Int,
) -> Int:
    var p = pos

    while p < size:
        if data[unsafe_offset=p] == 10:
            return p + 1

        p += 1

    return size
```

### What this does

`station_equal` compares two station strings stored as bytes in the memory-mapped file. It checks each byte from the two offsets and returns `False` as soon as it finds a mismatch.

This function is used when two hash values match, but the code still needs to verify the actual station name to avoid hash collisions.

`next_line` scans forward from a byte offset until it reaches a newline character (`10` in ASCII). Once it finds a newline, it returns the position immediately after it. This is used when splitting the file into worker chunks to ensure each worker starts and ends on valid record boundaries.

This is a crucial performance choice: workers do not just divide the file by byte count; they move to the next newline so each worker processes complete lines.

---

## 4. Hash-table insertion and aggregation

```mojo
@always_inline
def update_table(
    table: Pointer[Entry, MutUntrackedOrigin],
    data: Pointer[UInt8, MutUntrackedOrigin],
    table_base: Int,
    station_hash: UInt64,
    station_offset: Int,
    station_length: Int,
    temperature: Int,
):
    var local_slot = Int(
        station_hash & UInt64(TABLE_MASK)
    )

    while True:
        var slot = table_base + local_slot

        ref entry = table[
            unsafe_offset=slot
        ]

        if not entry[0]:
            entry[0] = True
            entry[1] = station_hash
            entry[2] = station_offset
            entry[3] = station_length
            entry[4] = 1
            entry[5] = temperature
            entry[6] = temperature
            entry[7] = temperature

            return

        if (
            entry[1] == station_hash
            and entry[3] == station_length
        ):
            if station_equal(
                data,
                entry[2],
                station_offset,
                station_length,
            ):
                entry[4] += 1
                entry[5] += temperature

                if temperature < entry[6]:
                    entry[6] = temperature

                if temperature > entry[7]:
                    entry[7] = temperature

                return

        local_slot = (
            local_slot + 1
        ) & TABLE_MASK
```

### What this does

This is the core aggregation algorithm for one worker’s table.

The function starts with the station’s hash, masks it with `TABLE_MASK`, and uses that as the initial table slot. If the slot is empty, it inserts a new station record and initializes:

- count = 1
- total = temperature
- min = temperature
- max = temperature

If the slot is occupied, it checks whether the existing record is the same station by comparing:

- same hash
- same station name length
- actual byte contents using `station_equal`

When it matches, it updates:

- `count += 1`
- `total += temperature`
- `min = min(existing_min, temperature)`
- `max = max(existing_max, temperature)`

If the slot is occupied by a different station, it performs linear probing by incrementing the slot index until it finds a free slot or a match.

This is a standard open-addressing hash table strategy. It is optimized for high throughput and avoids expensive dictionary object churn.

---

## 5. Parsing one chunk of data and updating the worker table

```mojo
@always_inline
def process_chunk(
    data: Pointer[UInt8, MutUntrackedOrigin],
    table: Pointer[Entry, MutUntrackedOrigin],
    table_base: Int,
    start: Int,
    end: Int,
):
    var pos = start

    while pos < end:
        var station_start = pos
        var station_hash = FNV_OFFSET

        while data[unsafe_offset=pos] != 59:
            station_hash ^= UInt64(
                data[unsafe_offset=pos]
            )

            station_hash *= FNV_PRIME

            pos += 1

        var station_length = (
            pos - station_start
        )

        pos += 1

        var negative = False

        if data[unsafe_offset=pos] == 45:
            negative = True
            pos += 1

        var temperature: Int

        if data[unsafe_offset=pos + 1] == 46:
            temperature = (
                Int(data[unsafe_offset=pos]) * 10
                + Int(data[unsafe_offset=pos + 2])
                - 528
            )

            pos += 3

        else:
            temperature = (
                Int(data[unsafe_offset=pos]) * 100
                + Int(data[unsafe_offset=pos + 1]) * 10
                + Int(data[unsafe_offset=pos + 3])
                - 5328
            )

            pos += 4

        if negative:
            temperature = -temperature

        if (
            pos < end
            and data[unsafe_offset=pos] == 10
        ):
            pos += 1

        update_table(
            table,
            data,
            table_base,
            station_hash,
            station_start,
            station_length,
            temperature,
        )
```

### What this does

This function is the actual parsing engine for a worker shard.

The code loops over each line in a chunk and performs three tasks:

1. reads the station name until the semicolon `;`
2. hashes each byte of the station name using the FNV-1a hash
3. parses the temperature value using byte arithmetic instead of converting strings to floats

The station name is taken from the file bytes between `station_start` and `pos`, and the length is computed as `pos - station_start`.

The numeric parsing is intentionally clever and fast. Instead of converting strings such as `-3.9` or `+12.3` into actual numeric values via a parser, the program treats the ASCII bytes as digits and uses arithmetic:

- for values like `-1.0`, it reads `1`, `0`, then subtracts a constant offset
- for values like `-12.3`, it reads all digit bytes and computes the value with integer math

The `negative` flag handles minus signs. Once the temperature is parsed, it passes the station hash, byte offset, name length, and temperature into `update_table`.

This is one of the biggest performance wins in the implementation: it avoids expensive string parsing and object allocation.

---

## 6. Worker entry point

```mojo
async def run_worker(
    data: Pointer[UInt8, MutUntrackedOrigin],
    table: Pointer[Entry, MutUntrackedOrigin],
    table_base: Int,
    start: Int,
    end: Int,
):
    process_chunk(
        data,
        table,
        table_base,
        start,
        end,
    )
```

### What this does

This function is the async wrapper that each worker executes. It simply calls `process_chunk` with the correct memory pointer, table pointer, and byte range.

This is used with `TaskGroup` to allow multiple CPU workers to process different chunks of the file concurrently.

---

## 7. Merging one worker table into the final table

```mojo
def merge_entry(
    final_table: Pointer[Entry, MutUntrackedOrigin],
    data: Pointer[UInt8, MutUntrackedOrigin],
    source: Entry,
):
    var slot = Int(
        source[1] & UInt64(TABLE_MASK)
    )

    while True:
        ref target = final_table[
            unsafe_offset=slot
        ]

        if not target[0]:
            target = source
            return

        if (
            target[1] == source[1]
            and target[3] == source[3]
        ):
            if station_equal(
                data,
                target[2],
                source[2],
                source[3],
            ):
                target[4] += source[4]
                target[5] += source[5]

                if source[6] < target[6]:
                    target[6] = source[6]

                if source[7] > target[7]:
                    target[7] = source[7]

                return

        slot = (
            slot + 1
        ) & TABLE_MASK
```

### What this does

After all worker tables are populated, the results still need to be combined into a single final table.

`merge_entry` takes an occupied entry from a worker-local table and inserts it into the final hash table. It follows the same open-addressing pattern as `update_table`, but this time it merges aggregate values instead of inserting fresh records.

For a matching station, it combines:

- `count`
- `sum`
- `min`
- `max`

If the final table already contains the station, the code adds the worker counts and totals and keeps the lower minimum and higher maximum across all workers.

This is the reduction step that turns multiple parallel tables into one final result.

---

## 8. Merging all worker tables

```mojo
def merge_tables(
    worker_tables: Pointer[
        Entry,
        MutUntrackedOrigin,
    ],
    final_table: Pointer[
        Entry,
        MutUntrackedOrigin,
    ],
    data: Pointer[
        UInt8,
        MutUntrackedOrigin,
    ],
    workers: Int,
):
    var worker = 0

    while worker < workers:
        var base = (
            worker * TABLE_SIZE
        )

        var slot = 0

        while slot < TABLE_SIZE:
            var entry = worker_tables[
                unsafe_offset=base + slot
            ]

            if entry[0]:
                merge_entry(
                    final_table,
                    data,
                    entry,
                )

            slot += 1

        worker += 1
```

### What this does

This function walks every worker-owned table in memory and merges each occupied entry into the final table.

Because each worker writes a table of size `TABLE_SIZE`, the overall worker tables are laid out as a large contiguous block:

- worker 0 starts at offset `0`
- worker 1 starts at offset `TABLE_SIZE`
- worker 2 starts at offset `2 * TABLE_SIZE`
- etc.

The loop uses `base = worker * TABLE_SIZE` to index the beginning of each worker’s table.

This merge step is important because each worker computes a partial result for the same station set. The final table must reconcile all partial results into a correct global aggregate.

---

## 9. Comparing station names for sorted output

```mojo
def station_less(
    data: Pointer[UInt8, MutUntrackedOrigin],
    left_offset: Int,
    left_length: Int,
    right_offset: Int,
    right_length: Int,
) -> Bool:
    var length = left_length

    if right_length < length:
        length = right_length

    var i = 0

    while i < length:
        var left = data[
            unsafe_offset=left_offset + i
        ]

        var right = data[
            unsafe_offset=right_offset + i
        ]

        if left < right:
            return True

        if left > right:
            return False

        i += 1

    return left_length < right_length
```

### What this does

This helper compares two station names lexicographically.

It takes the offsets and lengths of the two strings, walks both in parallel, and compares byte by byte. If one character is smaller, it returns `True`; if larger, `False`.

If the common prefix is equal, then it compares the lengths so that shorter strings sort before longer ones.

This function is used only for final output ordering, not during insertion into the hash table.

---

## 10. Collecting and sorting table entries for printing

```mojo
def collect_entries(
    table: Pointer[Entry, MutUntrackedOrigin],
) -> List[Int]:
    var indexes = List[Int](
        capacity=TABLE_SIZE
    )

    var slot = 0

    while slot < TABLE_SIZE:
        if table[unsafe_offset=slot][0]:
            indexes.append(slot)

        slot += 1

    return indexes^


def sort_entries(
    mut indexes: List[Int],
    table: Pointer[Entry, MutUntrackedOrigin],
    data: Pointer[UInt8, MutUntrackedOrigin],
):
    var i = 1

    while i < len(indexes):
        var j = i

        while j > 0:
            var current_index = indexes[j]
            var previous_index = indexes[j - 1]

            var current = table[
                unsafe_offset=current_index
            ]

            var previous = table[
                unsafe_offset=previous_index
            ]

            if not station_less(
                data,
                current[2],
                current[3],
                previous[2],
                previous[3],
            ):
                break

            indexes[j] = previous_index
            indexes[j - 1] = current_index

            j -= 1

        i += 1
```

### What this does

`collect_entries` scans the final table and gathers the indexes of every occupied slot into a list.

`sort_entries` performs insertion sort on those indexes based on the lexical order of station names. It is not using a full-blown sort algorithm because the number of stations is usually not enormous, and this keeps the code compact and efficient enough for the final pass.

The comparison uses the earlier `station_less` helper to compare the actual station names in the mapped file.

The result is a list of table indexes ordered by station name, which is exactly what is needed to print the final dictionary in sorted order.

---

## 11. String building, averaging, and formatting

```mojo
def make_station_string(
    data: Pointer[UInt8, MutUntrackedOrigin],
    offset: Int,
    length: Int,
) -> String:
    var result = String(
        unsafe_uninit_length=length
    )

    var output = result.unsafe_as_bytes_mut()

    var i = 0

    while i < length:
        output[i] = data[
            unsafe_offset=offset + i
        ]

        i += 1

    return result^


def average_tenths(
    total: Int,
    count: Int,
) -> Int:
    if total >= 0:
        var quotient = total // count
        var remainder = total % count

        if remainder * 2 >= count:
            quotient += 1

        return quotient

    var positive = -total

    var quotient = positive // count
    var remainder = positive % count

    if remainder * 2 > count:
        quotient += 1

    return -quotient


def print_tenths(value: Int):
    if value < 0:
        var positive = -value

        print(
            "-",
            positive // 10,
            ".",
            positive % 10,
            sep="",
            end="",
        )

    else:
        print(
            value // 10,
            ".",
            value % 10,
            sep="",
            end="",
        )
```

### What this does

The program stores temperatures in tenths of a degree, not as regular float values. That is why it stores the sum and min/max as integer values representing tenths of a degree.

`average_tenths` calculates the average in tenths while rounding to the nearest tenth. It does this by checking whether the remainder is large enough to justify rounding up.

`print_tenths` is the formatter used to print a value such as:

- `12.3` becomes `123` in the internal representation and prints as `12.3`
- `-5.7` prints as `-5.7`

`make_station_string` reconstructs the station name by copying bytes from the memory-mapped file into a Mojo `String` object. This is used when the final output is printed to the console.

---

## 12. Final output generation

```mojo
def print_results(
    table: Pointer[Entry, MutUntrackedOrigin],
    data: Pointer[UInt8, MutUntrackedOrigin],
):
    var indexes = collect_entries(
        table
    )

    sort_entries(
        indexes,
        table,
        data,
    )

    print("{", end="")

    var first = True

    for index in indexes:
        var entry = table[
            unsafe_offset=index
        ]

        if not first:
            print(", ", end="")

        first = False

        var station = make_station_string(
            data,
            entry[2],
            entry[3],
        )

        print(
            station,
            "=",
            sep="",
            end="",
        )

        print_tenths(
            entry[6]
        )

        print("/", end="")

        print_tenths(
            average_tenths(
                entry[5],
                entry[4],
            )
        )

        print("/", end="")

        print_tenths(
            entry[7]
        )

    print("}")
```

### What this does

This function prints the final output in the expected 1BRC format.

It does the following:

1. gathers all occupied entries in the final table
2. sorts them by station name
3. prints a JSON-like map with all stations in order
4. prints the min / average / max values for each station

The average is computed from the total and count using `average_tenths`, while the min and max are already tracked during aggregation.

The output format is:

```text
{Abha=-5.7/18.0/27.4, ...}
```

The final loop ensures commas are inserted only between entries, not after the last one.

---

## 13. Main program: file loading, chunking, parallel processing, finishing

```mojo
def main() raises:
    var file_path = "../data/measurements.txt"
    var path = Path(file_path)

    if not path.exists():
        raise Error(
            "measurements.txt does not exist"
        )

    if not path.is_file():
        raise Error(
            "measurements.txt is not a file"
        )

    var file_size = path.stat().st_size

    if file_size == 0:
        print("{}")
        return

    var fd = external_call[
        "open",
        c_int,
        num_fixed_args=2,
    ](
        file_path
            .as_c_string_slice()
            .unsafe_ptr(),
        c_int(O_RDONLY),
    )

    if fd < 0:
        raise Error(
            "open() failed"
        )

    var null_address = Optional[
        Pointer[
            NoneType,
            MutUntrackedOrigin,
        ]
    ]()

    var data = external_call[
        "mmap",
        Pointer[
            UInt8,
            MutUntrackedOrigin,
        ],
    ](
        null_address,
        c_size_t(file_size),
        c_int(PROT_READ),
        c_int(MAP_PRIVATE),
        fd,
        Int64(0),
    )

    _ = external_call[
        "close",
        c_int,
    ](
        fd
    )

    var workers = parallelism_level()

    if workers < 1:
        workers = 1

    var starts = List[Int](
        capacity=workers
    )

    var ends = List[Int](
        capacity=workers
    )

    var chunk_size = (
        file_size // workers
    )

    var start = 0
    var worker = 0

    while worker < workers:
        var end: Int

        if worker == workers - 1:
            end = file_size

        else:
            end = (
                (worker + 1)
                * chunk_size
            )

            end = next_line(
                data,
                end,
                file_size,
            )

        starts.append(start)
        ends.append(end)

        start = end
        worker += 1

    var total_entries = (
        workers * TABLE_SIZE
    )

    var empty_entry: Entry = (
        False,
        UInt64(0),
        0,
        0,
        0,
        0,
        0,
        0,
    )

    var worker_tables = List[Entry](
        capacity=total_entries
    )

    var i = 0

    while i < total_entries:
        worker_tables.append(
            empty_entry
        )

        i += 1

    var final_storage = List[Entry](
        capacity=TABLE_SIZE
    )

    i = 0

    while i < TABLE_SIZE:
        final_storage.append(
            empty_entry
        )

        i += 1

    var worker_table_ptr = Pointer(
        to=worker_tables[0]
    ).unsafe_origin_cast[
        MutUntrackedOrigin
    ]()

    var final_table_ptr = Pointer(
        to=final_storage[0]
    ).unsafe_origin_cast[
        MutUntrackedOrigin
    ]()

    var group = TaskGroup()

    worker = 0

    while worker < workers:
        group.create_task(
            run_worker(
                data,
                worker_table_ptr,
                worker * TABLE_SIZE,
                starts[worker],
                ends[worker],
            )
        )

        worker += 1

    group.wait()

    merge_tables(
        worker_table_ptr,
        final_table_ptr,
        data,
        workers,
    )

    print_results(
        final_table_ptr,
        data,
    )

    var opaque = data.unsafe_bitcast[
        NoneType
    ]()

    var unmap_result = external_call[
        "munmap",
        c_int,
    ](
        opaque,
        c_size_t(file_size),
    )

    if unmap_result != 0:
        print(
            "warning: munmap() failed"
        )

    _ = worker_tables
    _ = final_storage
    _ = starts
    _ = ends
```

### What this does

This is the orchestration section that brings the program together.

Step by step:

1. open the weather file and validate it exists and is a regular file
2. use `mmap` to map the entire file into memory for fast access
3. determine how many workers the machine should use via `parallelism_level()`
4. divide the file into chunks and align them to newline boundaries using `next_line`
5. allocate a worker-local table for each worker plus one final table
6. create a `TaskGroup` and launch one worker per chunk
7. each worker parses its chunk and updates its local hash table
8. merge all worker tables into the final global hash table
9. sort and print the station results
10. unmap the file and release resources

This is the main reason the Mojo version is so fast: it combines memory-mapped I/O, concurrency, hash-based aggregation, and integer-only parsing into a single tight pipeline.

---

## Summary

The file takes a data-processing strategy that is classic for large datasets:

- simple, raw byte access
- hash-based grouping
- parallelism by file sharding
- reduction after computation
- careful numeric encoding for speed

The optimization choices are not accidental. They are designed around the real bottlenecks of the problem: CPU time, memory access, and the cost of parsing text. The result is a program that is designed to process the dataset at near-memory bandwidth limits rather than at the speed of normal Python string processing.
