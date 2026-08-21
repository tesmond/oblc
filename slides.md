---
theme: default
title: One Billion Rows in Python
titleTemplate: '%s — One Billion Row Challenge'
info: |
  ## One Billion Rows  in Python
  So how fast can Python go?
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

<<< @/1_naive_python/naive.py python {1,3,5-7|8-10|12-19|21-29}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Model each station with explicit fields'
    body: 'A typed Stats struct records count, sum, minimum, and maximum. Default infinities allow comparison logic for first result.'
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

# Same algorithm, new runtime

::left::

<<< @/2_naive_go/main.go go {13-27|29-45|47-53|55-65|76-100}{maxHeight:'320px'}

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
  - title: 'Set up CPU count based chunking'
    body: 'It reads CPU count and page size up front, then computes a base chunk size for the input file.'
  - title: 'Split work on newline boundaries'
    body: 'Each chunk end is moved to the next newline so each worker processes complete records.'
  - title: 'Multiprocess the file and format once'
    body: 'A process pool runs process_chunk across chunks, then one sorted print assembles the challenge output.'
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

<<< @/4_optimised_python/perf.py python {1-6|9-25|27-30|44-57|60-78|81-94|97-99|102-114|117-118}{maxHeight:'320px'}

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
  - title: 'Keep the exact same process'
    body: 'Read files in chunks'
  - title: 'Use one dictionary lookup path'
    body: 'The update branch is compact and repeatable, which gives PyPy a hot loop to optimise.'
  - title: 'Run the same chunked worker flow under JIT'
    body: 'The program still splits, maps, and processes byte ranges in parallel, but now the runtime specialises repeated operations.'
  - title: 'Merge with explicit update logic'
    body: 'The merge pass combines and recalculates the values from the dicts.'
  - title: 'Print the sorted output'
    body: 'Main still executes one chunk-map-merge pipeline and prints the same sorted station format.'
    metric: '5 sec'
    note: 'Roughly 6× faster than the CPython run by changing runtime and preserving the same algorithm.'
---

<div class="eyebrow">6 · PyPy</div>

# Let the JIT optimise the hot loops

::left::

<<< @/5_pypy_optimised_python/perf.py python {9-25|61-79|82-95|103-115|118-119}{maxHeight:'300px'}

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

# Reach for Polars

::left::

<<< @/6_relax_dependencies/main.py python {1,7-16|17-22|23-24|27-32}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Open DuckDB in process'
    body: 'The Python script connects to an embedded DuckDB engine, so no separate database service is required.'
  - title: 'Express all aggregates in SQL'
    body: 'The query asks for min, avg, and max per station and aliases each metric for clean downstream formatting.'
  - title: 'Read CSV defining the schema'
    body: 'read_csv sets delimiter, header behavior, and column types so DuckDB can parse the text efficiently.'
  - title: 'Group and sort inside the engine'
    body: 'Aggregation and ordering happen in DuckDB, which keeps heavy compute out of the Python loop.'
  - title: 'Format only the returned rows'
    body: 'Python iterates over already-aggregated station rows and prints the final challenge string.'
    metric: '10.5 sec'
    note: 'The performance comes from delegating parsing and aggregation to a vectorized SQL engine.'
---

<div class="eyebrow">7 · DuckDB</div>

# Generate the output through SQL

::left::

<<< @/7_duckdb/main.py python {1,6|7-11|12-17|18-20|22-27}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
class: go-walkthrough
walkthrough:
  - title: 'Define data types'
    body: 'Fixing the data types allows for lower memory use than Python.'
  - title: 'Custom hash table to more quickly upsert and retrieve data'
    body: 'Compact statistics, byte-range chunks, and fixed-size station slots establish the exact memory layout before any rows are processed.'
  - title: 'Parse the bytes, gather city name and temparature'
    body: 'Faster text processing avoiding data type conversion.'
  - title: 'Merge the generated hash tables'
    body: 'Iterate through the hash tables and calculate the final city temperature values.'
  - title: 'Start processing memory mapped file'
    body: 'Start the file chunking process.'
  - title: 'Generate chunk sizes'
    body: 'Find the new line after a chunk to determine the chunk file size.'
  - title: 'Process each chunk within a separate go routine.'
    body: 'Create a worker loop and process all workers simultaneously.'
  - title: 'Convert integers back to floats'
    body: 'Ready the calculated values for display.'
  - title: 'Round and write the final values'
    body: 'The average is rounded with integer arithmetic, then average and maximum are appended before the completed result is printed once.'
    metric: '2 sec'
    note: 'Maximum control gives the fastest result, but the most maintenance burden.'
---

<div class="eyebrow">8 · What about Go?</div>

# Batch processed, merged, sorted and output

::left::

<<< @/8_what_about_go/perf.go go {20-42|45-73|75-100|102-122|129-151|172-186|190-199|238-251|253-264}{maxHeight:'320px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Load the database driver through database/sql'
    body: 'Register the DuckDB driver; the rest of the program uses Go’s standard database interface.'
  - title: 'Open the in-process database'
    body: 'The connection has normal Go lifecycle management and closes when main returns.'
  - title: 'Run the same SQL query'
    body: 'DuckDB still performs CSV parsing, grouping, aggregation, and ordering.'
  - title: 'Print the result rows'
    body: 'Iterate through results row and format the output.'
    metric: '6.5 sec'
    note: 'The Go binding is faster than the same Python solution.'
---

<div class="eyebrow">9 · Go + DuckDB</div>

# Same query different language

::left::

<<< @/9_what_about_go_duckdb/main.go go {1-10|12-17|19-37|39-53}{maxHeight:'320px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
---

<div class="eyebrow">Closing comparison</div>

| Approach | Runtime | Notes |
| --- | ---: | --- | 
| Naive Python | 6m 7s | Quickest to code |
| Naive Go | 1m 19s | Compiled and straight forward |
| Optimised Python (CPython) | 28.9s | Increased code complexity |
| Optimised Python (PyPy) | 5s | Free performance boost with new runtime |
| Polars | 10s | Straightforward, fast and easy to maintain |
| DuckDB (Python) | 10.5s | Great if you know SQL, highly maintainable |
| Optimised Go | 2s | Fastest option |
| DuckDB (Go) | 6.5s | SQL ergonomics but with faster processing | 


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

<<< @/10_mojo_optimised/perf.mojo mojo {1-29|32-50|79-100|138-156|162-183|196-204|223-260}{maxHeight:'320px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />


---
layout: two-cols-header
transition: fade
walkthrough:
  - title: 'Cheat'
    body: 'For the fastest solution just bridge from Python into C.'
    metric: '1.9 sec'
---

<div class="eyebrow">11 · But Wait There's More</div>

# Python the standard way

::left::

<<< @/11_cpython/main.py python {1-13}{maxHeight:'280px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

