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