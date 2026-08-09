package main

import (
	"bufio"
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
)

type Stats struct {
	Count float64
	Sum   float64
	Min   float64
	Max   float64
}

func newStats() Stats {
	return Stats{
		Count: 0,
		Sum:   0,
		Min:   math.Inf(1),
		Max:   math.Inf(-1),
	}
}

func processFile(path string) (map[string]Stats, error) {
	data := make(map[string]Stats)

	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		if line == "" {
			continue
		}

		parts := strings.Split(line, ";")
		station := parts[0]

		value, err := strconv.ParseFloat(parts[1], 64)
		if err != nil {
			return nil, err
		}

		stats, exists := data[station]
		if !exists {
			stats = newStats()
		}

		stats.Count++
		stats.Sum += value
		stats.Min = math.Min(stats.Min, value)
		stats.Max = math.Max(stats.Max, value)

		data[station] = stats
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return data, nil
}

func main() {
	path := "../data/measurements.txt"

	data, _ := processFile(path)

	stations := make([]string, 0, len(data))
	for station := range data {
		stations = append(stations, station)
	}

	sort.Strings(stations)

	fmt.Print("{")
	for _, station := range stations {
		stats := data[station]
		avg := stats.Sum / stats.Count

		fmt.Printf(
			"%s/%.1f/%.1f/%.1f\n",
			station,
			stats.Min,
			avg,
			stats.Max,
		)
	}
	fmt.Print("}")
}
