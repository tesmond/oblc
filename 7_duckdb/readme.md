# Alternative solution DuckDB

DuckDB is an in-process analytical database engine that can directly query CSV files using SQL. It parallelises across all available CPU cores automatically.

The implementation delegates the entire computation to DuckDB via a single SQL query — `read_csv` loads the file, `GROUP BY` computes the aggregates, and `ORDER BY` sorts the output. The Python code just to format and print the result.

The processing time for this is 10.5 seconds.