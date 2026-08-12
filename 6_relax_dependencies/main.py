import polars as pl

def main():
    path = "../data/measurements.txt"

    df = (
        pl.scan_csv(
            path,
            separator=";",
            has_header=False,
            new_columns=["station", "value"],
            schema_overrides={"station": pl.Utf8, "value": pl.Float16},
            quote_char=None,
            try_parse_dates=False,
            ignore_errors=True
        )
        .group_by("station")
        .agg(
            pl.min("value").alias("min"),
            pl.mean("value").alias("mean"),
            pl.max("value").alias("max"),
        )
        .collect(engine="streaming")
        .sort("station")
    )

    print("{")
    print(",".join(
        f"{row['station']}={row['min']:.1f}/{row['mean']:.1f}/{row['max']:.1f}"
        for row in df.iter_rows(named=True)
    ), end="")
    print("}")


if __name__ == "__main__":
    main()