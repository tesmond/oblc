package main

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

const tableSize = 1 << 12 // 4096 slots

type slot struct {
	klen  uint8
	key   [100]byte
	stats cityStats
}

type hashTable struct {
	slots [tableSize]slot
}

// upsert running stats using FNV-1a
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

	allData, err := syscall.Mmap(
		int(f.Fd()),
		0,
		int(fileSize),
		syscall.PROT_READ,
		syscall.MAP_SHARED,
	)
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

	results := make([]*hashTable, len(chunks))

	var wg sync.WaitGroup
	wg.Add(len(chunks))
	for i := range chunks {
		go func(i int) {
			defer wg.Done()
			results[i] = processChunk(
				allData[chunks[i].start:chunks[i].end],
			)
		}(i)
	}

	wg.Wait()

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

func main() {
	filePath := flag.String(
		"file",
		"../data/measurements.txt",
		"Path to measurements file",
	)
	flag.Parse()

	if err := readFileInChunks(*filePath); err != nil {
		log.Fatal(err)
	}
}
