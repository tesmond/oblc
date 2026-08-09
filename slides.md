---
theme: default
title: One Billion Rows, Nine Ways
titleTemplate: '%s — One Billion Row Challenge'
info: |
  ## One Billion Rows, Nine Ways
  A practical performance journey through Python, Go, Polars, and DuckDB.
author: Andrew
colorSchema: dark
highlighter: shiki
lineNumbers: true
transition: slide-left
mdc: true
---

<div class="eyebrow">A code-led performance story</div>

# One Billion Rows,<br><span class="accent">Nine Ways</span>

<div class="mt-10 text-xl muted">How far can Python go—and when should you reach for Go, Polars, or DuckDB?</div>

<div class="abs bottom-10 left-12 text-sm muted">One Billion Row Challenge · 20-minute walkthrough</div>

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
    metric: '7 min 38 sec'
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
walkthrough:
  - title: 'Discover the machine constraints'
    body: 'CPU count determines the worker count; the operating-system page size determines valid mmap offsets.'
  - title: 'Map and process one safe byte range'
    body: 'Each worker maps only its assigned region, seeks past page alignment padding, and reads complete lines.'
  - title: 'Parse fixed-point temperatures as bytes'
    body: 'The input grammar is known: optional sign, one or two integer digits, decimal point, one fractional digit. Integer tenths avoid float conversion.'
  - title: 'Aggregate without decoding station names'
    body: 'City keys remain bytes throughout the hot loop. Count, sum, minimum, and maximum mutate in a local dictionary.'
  - title: 'Respect mmap page alignment'
    body: 'Chunk starts are rounded down to a valid mapping boundary, then the worker seeks forward to its true starting byte.'
---

<div class="eyebrow">3 · Optimised Python</div>

# Remove conversion cost from the hot path

::left::

<<< @/3_optimised_python/perf.py python {1-6|9-22|25-43|46-59|62-64}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Merge worker-local aggregates'
    body: 'The reduction combines counts and sums, then chooses the global minimum and maximum for each city.'
  - title: 'Create newline-aligned chunks'
    body: 'Equal byte ranges are moved forward to the next newline so a record is never split between workers.'
  - title: 'Fan chunks out to processes'
    body: 'A multiprocessing pool bypasses the GIL for the CPU-heavy parsing loop. Each process returns its own dictionary.'
  - title: 'Decode once, at the presentation boundary'
    body: 'Only final station names are decoded. Integer tenths are scaled back to decimal temperatures while formatting.'
    metric: '33 sec'
    note: 'Parallel byte processing is fast, but it is now coupled to the exact file format.'
---

<div class="eyebrow">3 · Optimised Python</div>

# Partition, process, reduce

::left::

<<< @/3_optimised_python/perf.py python {67-79|82-97|99-102|104-116}{maxHeight:'300px'}

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

<div class="eyebrow">4 · PyPy</div>

# Let the JIT specialize the hot loops

::left::

<<< @/4_pypy_optimised_python/perf.py python {9-27|30-44|52-65|68-87|89-123}{maxHeight:'300px'}

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

<div class="eyebrow">5 · Relax dependencies</div>

# Let a columnar engine do the heavy lifting

::left::

<<< @/5_relax_dependencies/main.py python {1,3-5|6-16|17-25|27-36}{maxHeight:'300px'}

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

<div class="eyebrow">6 · DuckDB</div>

# Express the operation as a query

::left::

<<< @/6_duckdb/main.py python {1,3-5|6-11|12-17|18-20|22-31}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Write fixed-point output without formatting overhead'
    body: 'Temperatures are integer tenths. writeDecimal appends digits directly to a reusable strings.Builder.'
  - title: 'Pre-allocate a cache-friendly table'
    body: 'A station key, its byte length, and its four statistics live in fixed-size slots. The known station count keeps load low.'
  - title: 'Own the hash-table hot path'
    body: 'FNV-1a and linear probing replace a general-purpose map. Insert and update logic mutate the slot in place.'
  - title: 'Parse directly from the mmap byte slice'
    body: 'The loop locates the semicolon, decodes integer tenths, advances past the newline, and immediately updates the local table.'
  - title: 'Reduce worker-local tables after parsing'
    body: 'Only populated slots are visited. Counts, sums, minima, and maxima merge into the final map.'
---

<div class="eyebrow">7 · What about Go?</div>

# Control data layout and the hot loop

::left::

<<< @/7_what_about_go/perf.go go {18-32|34-58|60-89|91-118|120-145}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
walkthrough:
  - title: 'Validate the workload and open the file'
    body: 'Worker count is bounded by the file size, and ordinary errors are wrapped before entering the optimized path.'
  - title: 'Map the file once and advise sequential access'
    body: 'All workers share a read-only memory mapping. MADV_SEQUENTIAL tells the operating system how pages will be consumed.'
  - title: 'Partition on record boundaries'
    body: 'Each nominal chunk end moves to the next newline, producing disjoint slices that contain complete records.'
  - title: 'Give every goroutine private state'
    body: 'Each goroutine writes to its own hash table, so the parsing loop needs no lock. A WaitGroup provides the join point.'
  - title: 'Reduce, sort, and format once'
    body: 'Local tables merge after all workers finish; a single builder produces the ordered output.'
    metric: '2 sec'
  - title: 'Expose a normal command-line entry point'
    body: 'The optimized engine is wrapped behind a file flag and conventional error handling.'
    note: 'Maximum control delivers the fastest result, with the largest maintenance burden.'
---

<div class="eyebrow">7 · What about Go?</div>

# Map once, split safely, fan out

::left::

<<< @/7_what_about_go/perf.go go {147-173|175-183|184-198|200-212|213-245|248-255}{maxHeight:'300px'}

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

<div class="eyebrow">8 · Go + DuckDB</div>

# Keep the query; change the binding

::left::

<<< @/8_what_about_go_duckdb/main.go go {1-10|12-17|19-37|39-53}{maxHeight:'300px'}

::right::

<StepExplain :steps="$frontmatter.walkthrough" />

---
layout: two-cols-header
---

<div class="eyebrow">Closing comparison</div>

# There is no single “best” implementation

| Approach | Runtime | What you buy | What you pay |
| --- | ---: | --- | --- |
| Naive Python | 7m 38s | Maximum clarity | Per-row interpreter cost |
| Naive Go | 1m 19s | Typed, low-overhead loop | More ceremony |
| Optimised Python + PyPy | 5s | Major speed with Python source | Format-specific tuning + runtime choice |
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

<div class="mt-10 text-2xl muted">Text → bytes · floats → integer tenths · one loop → parallel chunks · hand-written logic → specialized engine</div>

<div class="mt-12 runtime">Measure first. Optimize second. Simplify whenever you can.</div>
