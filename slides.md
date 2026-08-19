---
theme: default
title: One Billion Rows in Python
titleTemplate: '%s — One Billion Row Challenge'
info: |
  ## One Billion Rows  in Python
  A practical performance journey through Python + some comeos
author: Andrew
colorSchema: dark
highlighter: shiki
lineNumbers: true
transition: slide-left
mdc: true
---

<div class="eyebrow">Python Performance Presentation</div>

# One Billion Row Challenge<br>

<div class="mt-10 text-xl muted">How slow is Python?</div>

---
layout: two-cols-header
transition: fade
---

<div class="eyebrow">Introduction</div>

# The challenge: summarize a 1 billion row CSV

::left::

<div class="text-lg leading-8">

For every station sort by city name and calculate:
<ul>
<li>minimum temperature</li>
<li>average temperature</li>
<li>maximum temperature</li>
</ul>

</div>

::right::

```text
London;12.4
Oslo;-3.7
London;15.1
```

<div class="mt-6 text-lg">

<ul>
<li>1 billion rows</li>
<li>up to 10,000 UTF-8 cities</li>
<li>temperatures from -99.9 to 99.9</li>
<li>; column separator</li>
<li>14 gb text file</li>
</ul>

</div>

---

<div class="eyebrow">0 · System Baseline </div>

# Get the optimal baseline

```bash
# Step 1: Run direct I/O read test
$ dd if=measurements.txt of=/dev/null bs=4M iflag=direct status=progress
```
<br>

# Runtime

<div class="text-lg leading-8">Run a simple CLI command to read a file directly to gather processing time.</div>
<div class="text-lg leading-8">The goal generated is 1.6 seconds.</div>

---
layout: two-cols-header
walkthrough:
  - title: 'Open the file and configure the parser'
    body: 'The standard-library CSV reader handles the semicolon delimiter. A dictionary will hold one running aggregate per city.'
  - title: 'Turn each row into a key and value'
    body: 'Every record becomes a Python string lookup and a float conversion inside the hot loop.'
  - title: 'Update count, sum, minimum, and maximum'
    body: 'The dictionary value is a compact four-item list. Existing cities mutate in place; new cities seed all four values.'
  - title: 'Sort and format only after aggregation'
    body: 'The average is deferred until output, avoiding a division on every row.'
    metric: '6 min 7 sec'
    note: 'The clear baseline exposes the cost of Python-level work per record.'
---

<div class="eyebrow">1 · Naive Python</div>

# Start with the simplest baseline

::left::

<<< @/1_naive_python/naive.py python {1,4,6-8|9-11|13-20|23-28}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Model each station with explicit fields'
    body: 'A typed Stats struct records count, sum, minimum, and maximum. Sentinel infinities make the first comparison correct.'
  - title: 'Stream the file one line at a time'
    body: 'bufio.Scanner keeps the baseline algorithm simple and bounds the amount of input held by the reader.'
  - title: 'Split and parse the record'
    body: 'The city stays a string; the temperature is parsed as a float64. This mirrors the Python baseline closely.'
  - title: 'Update a typed map value'
    body: 'The same four aggregates are updated, but without Python object dispatch on every operation.'
  - title: 'Sort keys and format the result'
    body: 'Map iteration is unordered, so station names are collected and sorted before output.'
    metric: '1 min 19 sec'
    note: 'About 5.8× faster without changing the algorithm.'
---

<div class="eyebrow">2 · Naive Go</div>

# Keep the algorithm; change the runtime

::left::

<<< @/2_naive_go/main.go go {13-27|29-45|47-53|55-65|75-100}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: center
transition: fade
---

<div class="eyebrow">3 · Profiling naive Python</div>

# Sampling profiler

<div class="w-full h-full flex items-center justify-center mt-4">
  <ProfileStep />
</div>

---
layout: two-cols-header
walkthrough:
  - title: 'Set up CPU-aware chunking'
    body: 'It reads CPU count and page size up front, then computes an even base chunk size for the input file.'
  - title: 'Split work on newline boundaries'
    body: 'Each chunk end is moved to the next newline so no worker starts or ends in the middle of a record.'
  - title: 'Fan out workers and format once'
    body: 'A process pool runs process_chunk across chunks, then one final sorted print assembles the challenge output.'
  - title: 'Map only the bytes a worker needs'
    body: 'Each worker aligns its mmap offset to page boundaries, seeks to its true start, and streams lines in-place.'
  - title: 'Parse temperatures as integer tenths'
    body: 'to_int converts ASCII bytes directly, handling sign and one- or two-digit whole numbers without float parsing.'
  - title: 'Update station stats in a tight loop'
    body: 'For every line, it slices city and value, then mutates count, sum, min, and max in a compact list.'
  - title: 'Keep mmap offsets OS-safe'
    body: 'align_offset snaps chunk starts to page multiples, which avoids mmap offset errors on macOS and Linux.'
  - title: 'Reduce worker dictionaries deterministically'
    body: 'merge_results combines per-city aggregates from every process into one final dictionary.'
  - title: 'Run the optimized path under CPython'
    body: 'The main guard executes the full chunk-map-merge pipeline with no extra orchestration code in the hot path.'
    metric: '28.9 sec'
    note: 'Most of the speedup comes from byte-level parsing, mmap reads, and parallel workers.'
---

<div class="eyebrow">4 · Optimised Python</div>

# Partition, parse, and merge in one pass

::left::

<<< @/4_optimised_python/perf.py python {1-6|9-25|27-30|43-56|59-77|80-93|96-98|101-113|116-117}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: center
transition: fade
---

<div class="eyebrow">5 · Profiling optimised Python</div>

# Sampling profiler

<div class="w-full h-full flex items-center justify-center mt-4">
  <img src="/4_optimised_python/profile.png" alt="Sampling heatmap for the optimized Python worker" class="w-full h-full object-contain" />
</div>

---
layout: two-cols-header
walkthrough:
  - title: 'Keep the fixed-point byte parser'
    body: 'The numeric hot path is unchanged: temperatures stay as integer tenths derived directly from ASCII bytes.'
  - title: 'Use one dictionary lookup path'
    body: 'The update branch is compact and repeatable, which gives PyPy a predictable hot loop to optimize.'
  - title: 'Run the same chunked worker flow under JIT'
    body: 'The program still splits, maps, and processes byte ranges in parallel, but now the runtime specializes repeated operations.'
  - title: 'Merge with explicit update logic'
    body: 'The merge pass applies direct count, sum, min, and max updates over worker dictionaries.'
  - title: 'Keep orchestration and output shape stable'
    body: 'Main still executes one chunk-map-merge pipeline and prints the same sorted station format.'
    metric: '5 sec'
    note: 'Roughly 6× faster than the CPython run by changing runtime and preserving the same algorithm.'
---

<div class="eyebrow">6 · PyPy</div>

# Let the JIT specialize the hot loops

::left::

<<< @/5_pypy_optimised_python/perf.py python {43-56|59-77|80-93|101-113|116-117}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Define a lazy CSV scan'
    body: 'scan_csv captures delimiter, missing header, and column names so Polars can own parsing and planning.'
  - title: 'Push aggregation into the query plan'
    body: 'group_by with min, mean, and max expresses the whole transformation before execution.'
  - title: 'Execute and order in engine'
    body: 'collect(engine="streaming") materializes the result, then sort("station") ensures deterministic output order.'
  - title: 'Format only final aggregate rows'
    body: 'Python loops over station-level rows to print the challenge output format in one pass.'
    metric: '10 sec'
    note: 'A concise, maintainable solution by leaning on a high-performance columnar runtime.'
---

<div class="eyebrow">6 · Relax dependencies</div>

# Let a columnar engine do the heavy lifting

::left::

<<< @/6_relax_dependencies/main.py python {1,3-5|6-16|17-25|27-36}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Open DuckDB in process'
    body: 'The Python script connects to an embedded DuckDB engine, so no separate database service is required.'
  - title: 'Express all aggregates in SQL'
    body: 'The query asks for min, avg, and max per station and aliases each metric for clean downstream formatting.'
  - title: 'Read CSV with explicit schema hints'
    body: 'read_csv sets delimiter, header behavior, and column types so DuckDB can parse the text efficiently.'
  - title: 'Group and sort inside the engine'
    body: 'Aggregation and ordering happen in DuckDB, which keeps heavy compute out of the Python loop.'
  - title: 'Format only the returned rows'
    body: 'Python iterates over already-aggregated station rows and prints the final challenge string.'
    metric: '10.5 sec'
    note: 'The performance comes from delegating parsing and aggregation to a vectorized SQL engine.'
---

<div class="eyebrow">7 · DuckDB</div>

# Express the operation as a query

::left::

<<< @/7_duckdb/main.py python {1,3-5|6-11|12-17|18-20|22-31}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
class: go-walkthrough
walkthrough:
  - title: 'Format integer tenths without fmt'
    body: 'The hot representation survives to output. Appending digits directly to one strings.Builder avoids converting every result through the general-purpose formatter.'
  - title: 'Make the working set predictable'
    body: 'Compact statistics, byte-range chunks, and fixed-size station slots establish the exact memory layout before any rows are processed.'
  - title: 'Hash each station once'
    body: 'FNV-1a consumes the station bytes and masks directly into the table. The power-of-two size turns bucket selection into one cheap operation.'
  - title: 'Update a slot without a Go map'
    body: 'Linear probing finds an empty or matching slot. New keys copy once; repeated keys mutate count, sum, minimum, and maximum in place.'
  - title: 'Slice the station from mapped bytes'
    body: 'The semicolon is located directly in the current byte slice. No line string or split result is allocated before the station lookup.'
  - title: 'Parse fixed-point temperature in place'
    body: 'Optional sign and one- or two-digit values become integer tenths through byte arithmetic, eliminating strconv and floating-point work.'
  - title: 'Merge only populated worker slots'
    body: 'Private hash tables keep the parse loop lock-free. Afterward, only occupied slots are converted to final station keys and combined.'
  - title: 'Bound workers and open the file once'
    body: 'The worker count follows available CPUs but never exceeds useful work. File validation and ordinary errors stay outside the hot path.'
  - title: 'Share one read-only memory map'
    body: 'Every goroutine reads the same mapped byte array. Sequential-access advice helps the operating system prepare the pages that will be consumed next.'
  - title: 'Cut chunks only at newlines'
    body: 'Each nominal boundary advances to the next record end, so workers receive disjoint slices and never repair partial measurements.'
  - title: 'Give each goroutine private state'
    body: 'Every goroutine parses one slice into its own table. With no shared writes, the hot loop needs neither a mutex nor atomic operations.'
  - title: 'Join before the final reduction'
    body: 'The WaitGroup establishes a clean phase boundary: parsing finishes first, then local tables merge and the small station key set is sorted.'
  - title: 'Reuse one output builder'
    body: 'Sorted stations append their names and minimum values to one buffer, avoiding a separate formatted string for every result field.'
  - title: 'Round and write the final values'
    body: 'The average is rounded with integer arithmetic, then average and maximum are appended before the completed result is printed once.'
    metric: '2 sec'
    note: 'Maximum control delivers the fastest result, with the largest maintenance burden.'
---

<div class="eyebrow">8 · What about Go?</div>

# One mapped file, private tables, one reduction

::left::

<<< @/8_what_about_go/perf.go go {18-32|34-58|60-67|68-89|91-97|98-118|120-145|147-173|175-182|184-198|200-212|213-218|220-231|232-245}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Load the database driver through database/sql'
    body: 'The blank import registers the DuckDB driver; the rest of the program uses Go’s standard database interface.'
  - title: 'Open and own the in-process database'
    body: 'The connection has normal Go lifecycle management and closes when main returns.'
  - title: 'Run the same analytical query'
    body: 'DuckDB still owns CSV parsing, grouping, three aggregates, and ordering. The SQL closely matches the Python version.'
  - title: 'Scan and format the result rows'
    body: 'Go crosses the native boundary for each aggregated station row, checks iterator errors, and joins the formatted parts.'
    metric: '6.5 sec'
    note: 'The Go binding is faster here while preserving the concise SQL solution.'
---

<div class="eyebrow">9 · Go + DuckDB</div>

# Keep the query; change the binding

::left::

<<< @/9_what_about_go_duckdb/main.go go {1-10|12-17|19-37|39-53}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
---

<div class="eyebrow">Closing comparison</div>

# There is no single “best” implementation

| Approach | Runtime | Benefits | Costs |
| --- | ---: | --- | --- |
| Naive Python | 6m 7s | Maximum clarity | Per-row interpreter cost |
| Naive Go | 1m 19s | Typed, low-overhead loop | More ceremony |
| Optimised Python (CPython) | 28.9s | Parallel byte processing | Format-specific tuning |
| Optimised Python (PyPy) | 5s | Same source, JIT-compiled hot loops | Runtime choice |
| Polars | 10s | Concise columnar pipeline | External dependency |
| DuckDB (Python) | 10.5s | Expressive SQL, automatic parallelism | Engine boundary |
| Optimised Go | 2s | Maximum control | Most complex code |
| DuckDB (Go) | 6.5s | SQL ergonomics with a Go host | Binding + dependency |

<div class="takeaway mt-8">
Start with the clearest solution. Profile the real bottleneck. Then choose the least-complex tool that meets the target.
</div>

<!--
The numbers here are the benchmark results reported by each section README. End on judgement:
speed, clarity, adaptability, and operations are all legitimate constraints.
-->

---
layout: two-cols-header
transition: fade
walkthrough:
  - title: 'Choose systems-level data structures'
    body: 'Compile-time constants and a fixed Entry tuple define a predictable open-addressed table instead of general-purpose Python objects.'
  - title: 'Insert directly into a worker table'
    body: 'The hash selects a starting slot; an empty entry receives the station offset, length, count, sum, minimum, and maximum in place.'
  - title: 'Resolve collisions and update in place'
    body: 'Matching hash and length trigger a byte comparison. Existing statistics mutate directly; collisions advance by linear probing.'
  - title: 'Hash station bytes while scanning'
    body: 'The parser walks to the semicolon and computes FNV at the same time, avoiding a separate station string and a second pass.'
  - title: 'Parse integer tenths from raw bytes'
    body: 'Optional sign handling and fixed ASCII arithmetic cover both one- and two-digit temperatures without allocating or parsing floats.'
  - title: 'Merge one populated entry'
    body: 'The reducer probes the final table, copies unseen stations, and combines count, sum, minimum, and maximum for matches.'
  - title: 'Schedule, join, reduce, and print'
    body: 'Native tasks receive private table regions. After the join, the tables merge and only the compact final result is formatted.'
    metric: 'under 2 sec'
    note: 'Mojo keeps Python-like syntax while exposing compilation, pointers, native tasks, and explicit memory layout.'
---

<div class="eyebrow">10 · Bonus · Mojo</div>

# Python-like syntax, systems-level control

::left::

<<< @/10_mojo_optimised/perf.mojo mojo {1-29|32-54|79-100|102-125|138-155|157-185|225-261}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />
