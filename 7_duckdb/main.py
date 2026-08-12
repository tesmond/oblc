import duckdb

def main():
    path = "../data/measurements.txt"

    result = duckdb.sql(f"""
        SELECT
            column0 AS station,
            MIN(column1) AS min,
            AVG(column1) AS mean,
            MAX(column1) AS max
        FROM read_csv(
            '{path}',
            delim=';',
            header=false,
            columns={{'column0': 'VARCHAR', 'column1': 'FLOAT'}}
        )
        GROUP BY station
        ORDER BY station
    """).fetchall()

    print("{")
    print(",".join(
        f"{row[0]}={row[1]:.1f}/{row[2]:.1f}/{row[3]:.1f}"
        for row in result
    ), end="")
    print("}")


if __name__ == "__main__":
    main()
