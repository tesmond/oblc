# Go 1BRC Implementation Walkthrough

This document explains the Go implementation in [8_what_about_go/perf.go](perf.go) section by section. The program is designed to solve the One Billion Row Challenge efficiently by combining:

- memory-mapped file reads
- CPU worker chunking
- hash-based aggregation
- parallel reduction
- sorted final output

The structure of the file follows the logic of the implementation, so this walkthrough is organized in the same order as the code appears.

---

## 1. Imports and file-level setup

```go
import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"os"
	"runtime"
	"sort"
	"strings"
	"sync"
	"syscall"

	"golang.org/x/sys/unix"
)
```

### What this does

These imports provide the tools needed for the program:

- `os` and `syscall`: file access and memory mapping
- `runtime`: CPU count detection
- `sync`: concurrency coordination via `WaitGroup`
- `sort`: sorting final station names
- `strings`: building the final output string
- `bytes`: efficient byte scanning and line splitting
- `flag`: command-line handling

This program is working almost entirely with raw bytes and integer arithmetic, which is exactly the kind of approach needed for high-throughput data processing.

The package imports are intentionally focused: the performance bottleneck is not the algorithmic logic so much as minimizing string parsing and allocations while reading a large file.

---

## 2. Formatting output values as decimals with one decimal place

```go
// writeDecimal writes v/10 as a fixed one-decimal-place value into b.
func writeDecimal(b *strings.Builder, v int) {
	if v < 0 {
		b.WriteByte('-')
		v = -v
	}
	if v >= 100 {
		b.WriteByte(byte('0' + v/100))
		b.WriteByte(byte('0' + (v/10)%10))
	} else {
		b.WriteByte(byte('0' + v/10))
	}
	b.WriteByte('.')
	b.WriteByte(byte('0' + v%10))
}
```

### What this does

This function converts an integer representation of a decimal value into its final textual form.

The idea is that the program stores temperatures as integer tenths of a degree, not as floating-point numbers. For example:

- `12.3` is stored as `123`
- `-5.7` is stored as `-57`

This avoids floating-point work and keeps comparisons and arithmetic cheap.

The function works like this:

- if the value is negative, it writes a minus sign and flips the sign
- it writes the integer part, splitting into the appropriate digit positions
- it writes the decimal point
- it writes the final digit as the tenth place

Example:

```go
writeDecimal(builder, 123)
```

produces:

```text
12.3
```

This is a fundamental optimization in this file: all temperature calculations are kept in integer space until the final output.

---

## 3. CPU count and aggregate record structure

```go
var cpuCount = runtime.NumCPU()

type cityStats struct {
	count int
	sum   int
	min   int
	max   int
}

type chunk struct {
	start int64
	end   int64
}
```

### What this does

`cpuCount` is the number of logical CPUs available on the machine, which is used to decide how many parallel workers will process the file.

`cityStats` is a per-station aggregate record. Each city keeps:

- `count`: number of readings
- `sum`: total temperature in tenths
- `min`: minimum temperature in tenths
- `max`: maximum temperature in tenths

`chunk` represents a file byte range to be processed by a worker.

This is the core data model: each worker builds a local hash table holding partial aggregates for a chunk, and those partial results are merged later into the final answer.

---

## 4. The hash table and slot layout

```go
const tableSize = 1 << 12 // 4096 slots

type slot struct {
	klen  uint8
	key   [100]byte
	stats cityStats
}

type hashTable struct {
	slots [tableSize]slot
}
```

### What this does

`tableSize` is set to `4096` slots. This is a fixed-size hash table used by each worker to store partial statistics for station names.

Each slot contains:

- `klen`: length of the station name
- `key`: a fixed-size byte array with the station name bytes
- `stats`: aggregate values for that station

This design is optimized for speed and compactness. A slot is effectively a small record for one station. The key is limited to 100 bytes, which is enough for the input station names in this dataset.

The `hashTable` is a local per-chunk structure, which means no heavy map allocation is needed for each line. The data stays compact and close to the processing logic.

---

## 5. FNV-1a hashing: how station names are turned into table indexes

```go
// upsert inserts or updates the station's running stats using FNV-1a
func (t *hashTable) upsert(key []byte, temp int) {
	h := uint32(2166136261)
	for _, b := range key {
		h ^= uint32(b)
		h *= 16777619
	}
	idx := h & (tableSize - 1)
	for {
		s := &t.slots[idx]
		if s.klen == 0 {
			s.klen = uint8(len(key))
			copy(s.key[:], key)
			s.stats = cityStats{count: 1, sum: temp, min: temp, max: temp}
			return
		}
		if s.klen == uint8(len(key)) && bytes.Equal(s.key[:s.klen], key) {
			s.stats.count++
			s.stats.sum += temp
			if temp < s.stats.min {
				s.stats.min = temp
			}
			if temp > s.stats.max {
				s.stats.max = temp
			}
			return
		}
		idx = (idx + 1) & (tableSize - 1)
	}
}
```

### What this does

This is the heart of the hash-table insertion logic.

#### The FNV-1a algorithm

FNV-1a works like this:

1. start with a fixed offset basis
2. for each byte in the input, XOR the hash with the byte
3. multiply the result by a large odd prime
4. continue until all bytes are processed

In this implementation:

- offset basis is `2166136261`
- prime is `16777619`

The code is:

```go
h := uint32(2166136261)
for _, b := range key {
    h ^= uint32(b)
    h *= 16777619
}
```

That means each station name is transformed into an integer hash. For example, for a city name `Abha`, the process looks roughly like:

- initialize `h` to a constant
- XOR with the first byte (`'A'`)
- multiply by the prime
- continue for each remaining byte
- final result is the hash

#### Why use FNV-1a here?

The program needs to quickly decide where a city should live in the hash table. Instead of storing a large Go map keyed by strings, it stores a fixed-size table of slots and computes a slot index with the hash.

The operation:

```go
idx := h & (tableSize - 1)
```

takes the low bits of the hash and turns them into a slot number. Because `tableSize` is `4096`, `tableSize - 1` is `4095`, and the bitmask keeps the index in range.

#### Collision handling

Hash functions can produce the same index for different stations. That is why the code uses linear probing:

```go
idx = (idx + 1) & (tableSize - 1)
```

This moves to the next slot until one of these two conditions becomes true:

- the slot is empty: insert the station
- the slot contains the same station name: update its stats

The important part is the equality check:

```go
if s.klen == uint8(len(key)) && bytes.Equal(s.key[:s.klen], key)
```

This confirms whether the matching hash slot actually holds the same city name, avoiding a false match caused by a hash collision.

This is a classic open-addressing hash table pattern, which is usually very fast when the table size is chosen well and the workload is mostly lookup/update operations.

---

## 6. Parsing one chunk of data and updating the table

```go
func processChunk(data []byte) *hashTable {
	t := new(hashTable)
	for len(data) > 0 {
		i := bytes.IndexByte(data, ';')
		city := data[:i]
		data = data[i+1:]

		neg := data[0] == '-'
		if neg {
			data = data[1:]
		}
		temp := int(data[0] - '0')
		data = data[1:]
		if data[0] != '.' {
			temp = temp*10 + int(data[0]-'0')
			data = data[1:]
		}
		data = data[1:] // decimal point
		temp = temp*10 + int(data[0]-'0')
		if neg {
			temp = -temp
		}
		data = data[2:] // last digit + '\n'

		t.upsert(city, temp)
	}
	return t
}
```

### What this does

This function processes a single chunk of the file and turns it into a local station summary table.

It loops until the chunk is exhausted and reads each row as raw bytes.

#### Step-by-step

1. Find the semicolon: 

```go
i := bytes.IndexByte(data, ';')
city := data[:i]
```

This extracts the station name bytes.

2. Advance past the semicolon:

```go
data = data[i+1:]
```

3. Check whether the temperature is negative:

```go
neg := data[0] == '-'
if neg {
    data = data[1:]
}
```

4. Parse the leading digit(s):

```go
temp := int(data[0] - '0')
data = data[1:]
if data[0] != '.' {
    temp = temp*10 + int(data[0]-'0')
    data = data[1:]
}
```

This reads the integer portion of the temperature while ignoring the decimal point.

5. Skip the decimal point and parse the last digit:

```go
data = data[1:] // decimal point
temp = temp*10 + int(data[0]-'0')
```

6. Apply sign if needed:

```go
if neg {
    temp = -temp
}
```

7. Skip the remainder of the line including newline:

```go
data = data[2:] // last digit + '\n'
```

8. Update the local hash table:

```go
t.upsert(city, temp)
```

This is a very fast parsing pattern because it works directly on ASCII bytes and uses integer arithmetic instead of converting strings to floats.

---

## 7. Merging partial tables into one final map

```go
func reduce(tables []*hashTable) map[string]*cityStats {
	final := make(map[string]*cityStats, 512)
	for _, t := range tables {
		for i := range t.slots {
			s := &t.slots[i]
			if s.klen == 0 {
				continue
			}
			city := string(s.key[:s.klen])
			if cur, ok := final[city]; ok {
				cur.count += s.stats.count
				cur.sum += s.stats.sum
				if s.stats.min < cur.min {
					cur.min = s.stats.min
				}
				if s.stats.max > cur.max {
					cur.max = s.stats.max
				}
			} else {
				stats := s.stats
				final[city] = &stats
			}
		}
	}
	return final
}
```

### What this does

After each worker processes its chunk, the worker produces a partial hash table. These tables must be merged into a single final aggregate table.

The function iterates over all worker hash tables, then scans each slot.

For each occupied slot:

- `city := string(s.key[:s.klen])` converts the station name back into a Go string
- it looks up that city in the final map
- if the city already exists, it adds the partial counts and totals
- it also updates the min and max values with the minimum and maximum seen across all chunks

This is the reduction step in the parallel processing pipeline.

The logic is important because the final output requires a single value for each station, not a value per chunk.

---

## 8. Reading the file and splitting it into chunks

```go
func readFileInChunks(filePath string) error {
	fi, err := os.Stat(filePath)
	if err != nil {
		return fmt.Errorf("stat file: %w", err)
	}

	fileSize := fi.Size()
	if fileSize == 0 {
		fmt.Println("{}")
		return nil
	}

	workers := cpuCount
	if int64(workers) > fileSize {
		workers = int(fileSize)
	}
	if workers < 1 {
		workers = 1
	}

	baseChunkSize := fileSize / int64(workers)

	f, err := os.Open(filePath)
	if err != nil {
		return fmt.Errorf("open file: %w", err)
	}
	defer f.Close()

	allData, err := syscall.Mmap(int(f.Fd()), 0, int(fileSize), syscall.PROT_READ, syscall.MAP_SHARED)
	if err != nil {
		return fmt.Errorf("mmap full file: %w", err)
	}
	defer func() {
		_ = syscall.Munmap(allData)
	}()
	_ = unix.Madvise(allData, unix.MADV_SEQUENTIAL)

	chunks := make([]chunk, 0, workers)
	start := int64(0)
	for i := 0; i < workers && start < fileSize; i++ {
		end := start + baseChunkSize
		if i == workers-1 || end >= fileSize {
			end = fileSize
		} else if nl := bytes.IndexByte(allData[end:], '\n'); nl >= 0 {
			end += int64(nl + 1)
		} else {
			end = fileSize
		}

		chunks = append(chunks, chunk{start: start, end: end})
		start = end
	}
```

### What this does

This is the file-loading and chunking backbone of the program.

#### Input validation

```go
fi, err := os.Stat(filePath)
```

The file is checked to ensure it exists and has a usable size.

If the file is empty, the output is simply `{}`.

#### Determining the worker count

```go
workers := cpuCount
if int64(workers) > fileSize {
    workers = int(fileSize)
}
if workers < 1 {
    workers = 1
}
```

This chooses a number of workers based on available CPU cores but prevents having more workers than data is worth splitting into.

#### Memory mapping

```go
allData, err := syscall.Mmap(...)
```

The file is mapped directly into memory as a byte slice. This avoids costly `Read` calls and allows the program to scan the file as if it were already in RAM.

```go
_ = unix.Madvise(allData, unix.MADV_SEQUENTIAL)
```

This line is a low-level OS hint to the kernel that the mapped file will be read in a forward, sequential pattern. `MADV_SEQUENTIAL` tells the operating system to optimize read-ahead and caching for streaming access, which matches this workload: the program walks the file linearly from start to finish. In other words, it helps the kernel predict that the next memory pages are likely to be needed soon, which can improve throughput when scanning large files.

The leading underscore is important: `unix.Madvise` returns an error value, but this program does not care if the hint fails. Using `_ = ...` discards the return value without causing the compiler to complain. In a performance-sensitive program, this is a deliberate choice to keep the optimization simple and avoid adding error handling to a call that is only a hint, not a required operation.

#### Chunk splitting

The program computes a base chunk size:

```go
baseChunkSize := fileSize / int64(workers)
```

Then each worker receives a range `[start:end)`.

The important detail is that each chunk is aligned to a newline boundary:

```go
else if nl := bytes.IndexByte(allData[end:], '\n'); nl >= 0 {
    end += int64(nl + 1)
}
```

This ensures each worker processes complete lines and never splits a record across two workers.

That matters because the program is parsing the file as a stream of bytes. If a chunk cut through the middle of a line, it would create invalid parsing and incorrect results.

---

## 9. Spawning worker goroutines and processing in parallel

```go
	results := make([]*hashTable, len(chunks))

	var wg sync.WaitGroup
	wg.Add(len(chunks))
	for i := range chunks {
		go func(i int) {
			defer wg.Done()
			results[i] = processChunk(allData[chunks[i].start:chunks[i].end])
		}(i)
	}

	wg.Wait()
```

### What this does

Each chunk is assigned to a worker goroutine.

The thread-safe coordination is handled by a `sync.WaitGroup`:

- `wg.Add(len(chunks))` counts how many workers should finish
- each goroutine calls `wg.Done()` when done
- `wg.Wait()` blocks until all chunks are processed

The actual task is:

```go
results[i] = processChunk(allData[chunks[i].start:chunks[i].end])
```

This means each goroutine receives a slice of the memory-mapped file representing one chunk and builds an independent `hashTable` summarizing all stations in that chunk.

This is the parallelism stage: instead of processing the entire dataset in one thread, the work is divided across CPU cores, each doing a local reduction.

---

## 10. Sorting final station names and writing the output

```go
	final := reduce(results)
	keys := make([]string, 0, len(final))
	for city := range final {
		keys = append(keys, city)
	}
	sort.Strings(keys)

	var b strings.Builder
	b.WriteByte('{')
	for i, city := range keys {
		if i > 0 {
			b.WriteString(", ")
		}

		val := final[city]
		b.WriteString(city)
		b.WriteByte('=')
		writeDecimal(&b, val.min)
		b.WriteByte('/')
		// average requires rounding: add half-divisor before truncating (negate for negative sums)
		half := val.count / 2
		if val.sum < 0 {
			half = -half
		}
		avg := (val.sum + half) / val.count
		writeDecimal(&b, avg)
		b.WriteByte('/')
		writeDecimal(&b, val.max)
	}
	b.WriteByte('}')

	fmt.Println(b.String())
	return nil
}
```

### What this does

This is the final assembly step of the program.

#### Sorting city names

```go
keys := make([]string, 0, len(final))
for city := range final {
    keys = append(keys, city)
}
sort.Strings(keys)
```

This extracts all final city names and sorts them alphabetically so the output matches the required format.

#### Building output with a string builder

```go
var b strings.Builder
b.WriteByte('{')
```

A `strings.Builder` is used to accumulate the output efficiently instead of repeatedly concatenating strings.

For each city in order:

- write the city name
- write `=`
- print the minimum value
- print `/`
- compute the rounded average
- print `/`
- print the maximum value

#### Rounding the average

The average calculation intentionally uses integer rounding logic:

```go
half := val.count / 2
if val.sum < 0 {
    half = -half
}
avg := (val.sum + half) / val.count
```

This approximates normal half-up rounding without using floating point. For a negative number, the sign is handled carefully so the program rounds consistently.

The result is a final output like:

```text
{Abha=-5.7/18.0/27.4, ...}
```

---

## 11. Main entry point and command-line argument handling

```go
func main() {
	filePath := flag.String("file", "../data/measurements.txt", "Path to measurements file")
	flag.Parse()

	if err := readFileInChunks(*filePath); err != nil {
		log.Fatal(err)
	}
}
```

### What this does

This is the actual program entry point.

It defines a command-line flag:

```go
-file ../data/measurements.txt
```

with a default path to the dataset. Then it calls:

```go
readFileInChunks(*filePath)
```

If that function returns an error, the program exits with a logged fatal error.

This keeps the program easy to rerun on different machine setups or different input files while still using a sensible default dataset location.

---

## Summary of the overall strategy

This Go program is designed around a few performance principles:

1. memory-map the file to avoid repeated reads
2. split the file into newline-safe chunks
3. process each chunk in parallel with a worker goroutine
4. aggregate per chunk in a compact hash table
5. combine partial aggregates during a reduction step
6. sort and print the final station values

The key optimization is that the program does almost all arithmetic in integers and keeps everything byte-oriented. That is what makes it practical for a dataset of this size.

The FNV-1a hash is central because it gives the program a way to map station names to compact hash-table slots quickly while still allowing collision-resolving probes when two names share an index.

In short, the program is not doing expensive string-heavy dictionary work per line. Instead, it uses raw bytes, integer math, parallel processing, and fixed-size hash tables to push the computation toward memory bandwidth rather than per-record overhead.
