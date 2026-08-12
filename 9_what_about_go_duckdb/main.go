package main

import (
	"database/sql"
	"fmt"
	"log"
	"strings"

	_ "github.com/marcboeker/go-duckdb"
)

func main() {
	db, err := sql.Open("duckdb", "")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	rows, err := db.Query(`
		SELECT
			column0 AS station,
			MIN(column1) AS min,
			AVG(column1) AS mean,
			MAX(column1) AS max
		FROM read_csv(
			'../data/measurements.txt',
			delim=';',
			header=false,
			columns={'column0': 'VARCHAR', 'column1': 'FLOAT'}
		)
		GROUP BY station
		ORDER BY station
	`)
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()

	var parts []string
	for rows.Next() {
		var station string
		var min, mean, max float64
		if err := rows.Scan(&station, &min, &mean, &max); err != nil {
			log.Fatal(err)
		}
		parts = append(parts, fmt.Sprintf("%s=%.1f/%.1f/%.1f", station, min, mean, max))
	}
	if err := rows.Err(); err != nil {
		log.Fatal(err)
	}

	fmt.Printf("{%s}\n", strings.Join(parts, ","))
}
