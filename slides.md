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

<div class="eyebrow">A code-led performance presentation</div>

# One Billion Rows,<br><span class="accent">ASAP</span>

<div class="mt-10 text-xl muted">How slow is Python?</div>

<div class="abs bottom-10 left-12 text-sm muted">One Billion Row Challenge in Python</div>

<!--
Welcome. This is not a language shoot-out. It is a tour of choices: data representation,
parallelism, runtimes, and delegation to specialized engines.
-->

---
layout: two-cols-header
transition: fade
---

<div class="eyebrow">0 · Introduction</div>

# The challenge: summarize a giant file

::left::

<div class="text-lg leading-8">

For every station, calculate <span class="accent">minimum / average / maximum</span> temperature and print stations in order.

</div>

<div class="takeaway mt-8">
The input is deliberately simple: <code>city;temperature</code>, one record per line.
</div>

::right::

```text
London;12.4
Oslo;-3.7
London;15.1
```

<div class="mt-6 text-lg">

<span class="accent">1 million rows</span> · up to 10,000 UTF-8 cities · temperatures with one decimal place

</div>

<!--
Set up the repeated task. The README frames this as a one-million-row variant of the
One Billion Row Challenge; the data shape is intentionally narrow and predictable.
-->

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

# Start with the clearest possible baseline

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
layout: two-cols-header
transition: fade
---

<div class="eyebrow">3 · Profiling naive Python</div>

# A heatmap turns “slow” into a line-level question

<span v-click class="profile-click-anchor" aria-hidden="true"></span>

::left::

<ProfileStep />

::right::

<div v-if="$clicks === 0" class="profile-copy">
  <div class="profile-kicker">Naive Python · main process</div>
  <h3>Every row pays interpreter costs</h3>
  <p>The hottest lines are iteration, float conversion, dictionary membership, and <code>min</code>/<code>max</code>. That evidence motivates bytes, fixed-point integers, and fewer Python operations per record.</p>
  <div class="takeaway mt-6">Heat means samples landed here—not that the line can be optimized in isolation.</div>
</div>

<div v-else class="profile-copy">
  <div class="profile-kicker">Optimised Python · worker process</div>
  <h3>The hotspot moves into parsing</h3>
  <p>After parallelization, a worker spends most samples in <code>process_line</code>, especially slicing and converting the temperature bytes. The next targets are fewer allocations, one dictionary lookup, and inlined comparisons.</p>
  <div class="takeaway mt-6"><code>--subprocesses</code> matters: profiling only the parent mostly shows it waiting for workers.</div>
</div>

<!--
Start with the naive profile. Click once to replace both the image and explanation with
the optimized worker profile. Sampling a multiprocessing parent alone mostly records
Condition.wait; follow child processes to profile process_chunk.
-->

---
layout: two-cols-header
walkthrough:
  - title: 'Read the machine constraints once'
    body: 'CPU count determines the number of chunks and workers; the operating-system page size will keep every mmap offset valid.'
  - title: 'Move chunk ends to record boundaries'
    body: 'Nominal byte ranges advance to the next newline, so no worker begins or ends halfway through a measurement.'
  - title: 'Send independent chunks to processes'
    body: 'A multiprocessing pool runs one process_chunk call per range, bypassing the GIL and returning worker-local dictionaries.'
  - title: 'Map and scan one safe byte range'
    body: 'Each worker page-aligns its mapping, seeks to the true start, and reads complete lines from only its assigned region.'
  - title: 'Parse fixed-point temperatures as bytes'
    body: 'The known numeric grammar becomes integer tenths through direct ASCII arithmetic, avoiding string decoding and float conversion.'
  - title: 'Aggregate each byte record locally'
    body: 'The semicolon locates the station and temperature. Byte keys and integer tenths keep conversion overhead out of the per-row dictionary update.'
  - title: 'Respect mmap page alignment'
    body: 'Chunk starts are rounded down to an operating-system page boundary; the worker then seeks forward to its exact start.'
  - title: 'Reduce worker-local aggregates'
    body: 'Counts and sums are added while minimum and maximum values are compared into one final dictionary.'
  - title: 'Keep multiprocessing import-safe'
    body: 'The main guard prevents spawned worker processes from executing the top-level orchestration again when they import this module.'
    metric: '28.9 sec'
    note: 'Parallel byte processing is much faster, but the implementation is now coupled to the exact input grammar.'
---

<div class="eyebrow">4 · Optimised Python</div>

# Partition, parse, and reduce in one pass

::left::

<<< @/4_optimised_python/perf.py python {1-6|9-25|27-30|43-56|59-77|80-93|96-98|101-113|116-117}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Keep the fixed-point byte parser'
    body: 'The numeric hot path is unchanged: temperatures remain integer tenths derived directly from ASCII bytes.'
  - title: 'Use one dictionary lookup per record'
    body: 'result.get(city) replaces a membership test followed by indexing. PyPy can specialize this stable branch pattern.'
  - title: 'Run the same chunk loop under a JIT'
    body: 'Repeated byte scanning and dictionary mutation give PyPy a hot loop it can compile while the program runs.'
  - title: 'Make the merge path explicit'
    body: 'try/except separates first-seen cities from updates; direct comparisons replace repeated min and max calls.'
  - title: 'Keep orchestration and output intact'
    body: 'The program still chunks, maps, reduces, sorts, and formats in the same overall shape.'
    metric: '5 sec'
    note: 'Roughly 6.6× faster than the CPython run by changing the runtime and tuning its hot branches.'
---

<div class="eyebrow">5 · PyPy</div>

# Let the JIT specialize the hot loops

::left::

<<< @/5_pypy_optimised_python/perf.py python {43-56|59-77|80-93|101-113|116-117}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Move the execution engine out of Python'
    body: 'Polars is imported as the native columnar engine; Python describes the work rather than performing every row operation.'
  - title: 'Declare a lazy, typed CSV scan'
    body: 'The separator, missing header, column names, and schema are specified up front so parsing can be planned efficiently.'
  - title: 'Aggregate as one streaming pipeline'
    body: 'Group-by, min, mean, max, collection, and sorting stay inside Polars. Streaming avoids materializing the full input first.'
  - title: 'Format only the small result set'
    body: 'Python iterates over aggregated station rows, not the original million input rows.'
    metric: '10 sec'
    note: 'A concise and maintainable solution, paid for with an external dependency.'
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
  - title: 'Use Python as the host language'
    body: 'The DuckDB module embeds an analytical database in the process; the path remains ordinary application configuration.'
  - title: 'Describe the requested aggregates'
    body: 'Minimum, average, and maximum are expressed directly in SQL and given names for result formatting.'
  - title: 'Teach DuckDB the file shape'
    body: 'read_csv receives the delimiter, header rule, and two column types, allowing the engine to parse the file itself.'
  - title: 'Group, order, and execute inside the engine'
    body: 'Parallel scanning, grouping, aggregation, and sorting stay in DuckDB; Python receives only the final rows.'
  - title: 'Format the compact query result'
    body: 'The remaining Python loop runs once per station rather than once per measurement.'
    metric: '10.5 sec'
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

<div v-click class="takeaway mt-8">
Start with the clearest solution. Profile the real bottleneck. Then choose the least-complex tool that meets the target.
</div>

<!--
The numbers here are the benchmark results reported by each section README. End on judgement:
speed, clarity, adaptability, and operations are all legitimate constraints.
-->

---
layout: center
transition: fade
---

<div class="eyebrow">The durable lesson</div>

# Performance is a series<br>of <span class="accent">representation choices.</span>

<div class="mt-10 text-2xl muted">Text → bytes · floats → integer tenths · one loop → parallel chunks</div>
<div class="mt-10 text-2xl muted">But hand-written logic is costly and long term a specialised engine may be optimal</div>

<div class="mt-12 runtime">Measure first. Optimize second. Simplify whenever you can.</div>

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
  - title: 'Create newline-aligned worker ranges'
    body: 'The mapped file is divided by the native parallelism level, with every intermediate boundary advanced to the next complete record.'
  - title: 'Schedule, join, reduce, and print'
    body: 'Native tasks receive private table regions. After the join, the tables merge and only the compact final result is formatted.'
    metric: 'under 2 sec'
    note: 'Mojo keeps Python-like syntax while exposing compilation, pointers, native tasks, and explicit memory layout.'
---

<div class="eyebrow">10 · Bonus · Mojo</div>

# Python-like syntax, systems-level control

::left::

<<< @/10_mojo_optimised/perf.mojo mojo {9-29|79-100|102-125|138-155|157-185|225-261|607-637|691-720}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />
