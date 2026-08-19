#define _GNU_SOURCE
#define PY_SSIZE_T_CLEAN

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Python.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#if defined(__linux__)
#include <sched.h>
#endif

#define LOCAL_TABLE_BITS 15
#define LOCAL_TABLE_SIZE (1u << LOCAL_TABLE_BITS)
#define LOCAL_TABLE_MASK (LOCAL_TABLE_SIZE - 1)

#define FINAL_TABLE_BITS 15
#define FINAL_TABLE_SIZE (1u << FINAL_TABLE_BITS)
#define FINAL_TABLE_MASK (FINAL_TABLE_SIZE - 1)

#define MAX_THREADS 256

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE __attribute__((always_inline)) inline
#define ALIGN64 __attribute__((aligned(64)))
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define FORCE_INLINE inline
#define ALIGN64
#endif

typedef struct {
    const char *name;
    uint64_t hash;
    int64_t sum;
    uint64_t count;
    int16_t min;
    int16_t max;
    uint16_t len;
} Entry;

typedef struct ALIGN64 {
    const char *start;
    const char *end;
    Entry *table;
    uint32_t thread_id;
    char _pad[64];
} Worker;

static FORCE_INLINE uint64_t read64(const void *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static FORCE_INLINE uint32_t read32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static FORCE_INLINE uint64_t rotl64(uint64_t x, unsigned k)
{
    return (x << k) | (x >> (64 - k));
}

static FORCE_INLINE uint64_t mum(uint64_t a, uint64_t b)
{
#if defined(__SIZEOF_INT128__)
    __uint128_t r = (__uint128_t)a * (__uint128_t)b;
    return (uint64_t)r ^ (uint64_t)(r >> 64);
#else
    uint64_t ah = a >> 32;
    uint64_t al = (uint32_t)a;
    uint64_t bh = b >> 32;
    uint64_t bl = (uint32_t)b;

    uint64_t x = al * bl;
    uint64_t y = ah * bl;
    uint64_t z = al * bh;
    uint64_t w = ah * bh;

    return x ^ rotl64(y, 21) ^ rotl64(z, 42) ^ w;
#endif
}

static FORCE_INLINE uint64_t hash_name(const char *s, size_t len)
{
    static const uint64_t P0 = UINT64_C(0xa0761d6478bd642f);
    static const uint64_t P1 = UINT64_C(0xe7037ed1a0b428db);
    static const uint64_t P2 = UINT64_C(0x8ebc6af09c88c6e3);
    static const uint64_t P3 = UINT64_C(0x589965cc75374cc3);

    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = P0 ^ (uint64_t)len;

    while (len >= 32) {
        uint64_t a = read64(p);
        uint64_t b = read64(p + 8);
        uint64_t c = read64(p + 16);
        uint64_t d = read64(p + 24);

        h = mum(a ^ P1, b ^ h) ^ mum(c ^ P2, d ^ P3);
        p += 32;
        len -= 32;
    }

    if (len >= 16) {
        uint64_t a = read64(p);
        uint64_t b = read64(p + 8);

        h = mum(a ^ P1, b ^ h);
        p += 16;
        len -= 16;
    }

    if (len >= 8) {
        h = mum(read64(p) ^ P2, h ^ P3);
        p += 8;
        len -= 8;
    }

    if (len >= 4) {
        h = mum((uint64_t)read32(p) ^ P1, h ^ P2);
        p += 4;
        len -= 4;
    }

    uint64_t tail = 0;
    switch (len) {
        case 3:
            tail |= (uint64_t)(unsigned char)p[2] << 16;
            /* fallthrough */
        case 2:
            tail |= (uint64_t)(unsigned char)p[1] << 8;
            /* fallthrough */
        case 1:
            tail |= (uint64_t)(unsigned char)p[0];
            break;
        default:
            break;
    }

    h = mum(h ^ tail ^ P0, P3 ^ (uint64_t)len);
    h ^= h >> 29;
    h *= UINT64_C(0x165667919e3779f9);
    h ^= h >> 32;

    return h | 1;
}

static FORCE_INLINE const char *find_semicolon(const char *p, const char *end)
{
#if defined(__AVX2__)
    const __m256i needle = _mm256_set1_epi8(';');

    while (LIKELY(p + 32 <= end)) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        __m256i eq = _mm256_cmpeq_epi8(v, needle);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(eq);

        if (mask)
            return p + __builtin_ctz(mask);

        p += 32;
    }
#elif defined(__aarch64__) || defined(__ARM_NEON)
    const uint8x16_t needle = vdupq_n_u8(';');

    while (LIKELY(p + 16 <= end)) {
        uint8x16_t v = vld1q_u8((const uint8_t *)p);
        uint8x16_t eq = vceqq_u8(v, needle);

#if defined(__aarch64__)
        uint64x2_t x = vreinterpretq_u64_u8(eq);
        if (vgetq_lane_u64(x, 0) | vgetq_lane_u64(x, 1)) {
            for (int i = 0; i < 16; ++i) {
                if (p[i] == ';')
                    return p + i;
            }
        }
#else
        uint64_t tmp[2];
        vst1q_u64(tmp, vreinterpretq_u64_u8(eq));
        if (tmp[0] | tmp[1]) {
            for (int i = 0; i < 16; ++i) {
                if (p[i] == ';')
                    return p + i;
            }
        }
#endif

        p += 16;
    }
#endif

    while (*p != ';')
        ++p;

    return p;
}

static FORCE_INLINE const char *parse_temperature(const char *p, int *value)
{
    int negative = (*p == '-');
    p += negative;

    int v;
    if (LIKELY(p[1] == '.')) {
        v = ((int)(unsigned char)p[0] - '0') * 10 + ((int)(unsigned char)p[2] - '0');
        p += 4;
    } else {
        v = ((int)(unsigned char)p[0] - '0') * 100 + ((int)(unsigned char)p[1] - '0') * 10 + ((int)(unsigned char)p[3] - '0');
        p += 5;
    }

    *value = negative ? -v : v;
    return p;
}

static FORCE_INLINE void local_add(
    Entry *restrict table,
    const char *restrict name,
    uint16_t len,
    uint64_t hash,
    int temperature)
{
    uint32_t slot = (uint32_t)hash & LOCAL_TABLE_MASK;

    for (;;) {
        Entry *e = &table[slot];

        if (LIKELY(e->hash == hash)) {
            if (LIKELY(e->len == len && memcmp(e->name, name, len) == 0)) {
                e->sum += temperature;
                e->count++;

                if (temperature < e->min)
                    e->min = (int16_t)temperature;

                if (temperature > e->max)
                    e->max = (int16_t)temperature;

                return;
            }
        } else if (e->hash == 0) {
            e->name = name;
            e->hash = hash;
            e->sum = temperature;
            e->count = 1;
            e->min = (int16_t)temperature;
            e->max = (int16_t)temperature;
            e->len = len;
            return;
        }

        slot = (slot + 1) & LOCAL_TABLE_MASK;
    }
}

static void *worker_main(void *arg)
{
    Worker *w = (Worker *)arg;

#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(w->thread_id % CPU_SETSIZE, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif

    const char *p = w->start;
    const char *end = w->end;
    Entry *restrict table = w->table;

    while (LIKELY(p < end)) {
        const char *name = p;
        const char *semi = find_semicolon(p, end);

        uint16_t len = (uint16_t)(semi - name);
        uint64_t hash = hash_name(name, len);

        int temperature;
        p = parse_temperature(semi + 1, &temperature);

        local_add(table, name, len, hash, temperature);
    }

    return NULL;
}

static FORCE_INLINE void merge_entry(Entry *restrict table, const Entry *restrict src)
{
    uint32_t slot = (uint32_t)src->hash & FINAL_TABLE_MASK;

    for (;;) {
        Entry *dst = &table[slot];

        if (dst->hash == src->hash) {
            if (dst->len == src->len && memcmp(dst->name, src->name, src->len) == 0) {
                dst->sum += src->sum;
                dst->count += src->count;

                if (src->min < dst->min)
                    dst->min = src->min;

                if (src->max > dst->max)
                    dst->max = src->max;

                return;
            }
        } else if (dst->hash == 0) {
            *dst = *src;
            return;
        }

        slot = (slot + 1) & FINAL_TABLE_MASK;
    }
}

static int entry_cmp(const void *ap, const void *bp)
{
    const Entry *a = *(Entry * const *)ap;
    const Entry *b = *(Entry * const *)bp;

    size_t min_len = a->len < b->len ? a->len : b->len;
    int r = memcmp(a->name, b->name, min_len);

    if (r)
        return r;

    return (a->len > b->len) - (a->len < b->len);
}

static FORCE_INLINE int64_t rounded_average(int64_t sum, uint64_t count)
{
    int64_t c = (int64_t)count;
    int64_t q = sum / c;
    int64_t r = sum % c;

    if (r >= 0) {
        if ((uint64_t)r * 2 >= count)
            ++q;
    } else {
        if ((uint64_t)(-r) * 2 > count)
            --q;
    }

    return q;
}

static FORCE_INLINE char *append_tenth(char *out, int64_t value)
{
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }

    uint64_t integer = (uint64_t)value / 10;
    unsigned frac = (unsigned)((uint64_t)value % 10);

    char digits[24];
    char *d = digits + sizeof(digits);

    do {
        *--d = (char)('0' + integer % 10);
        integer /= 10;
    } while (integer);

    size_t n = (size_t)((digits + sizeof(digits)) - d);
    memcpy(out, d, n);
    out += n;

    *out++ = '.';
    *out++ = (char)('0' + frac);

    return out;
}

static const char *next_record_boundary(const char *data, size_t size, size_t offset)
{
    if (offset == 0)
        return data;

    if (offset >= size)
        return data + size;

    const char *p = data + offset;
    const char *end = data + size;

    while (p < end && *p != '\n')
        ++p;

    if (p < end)
        ++p;

    return p;
}

static unsigned detect_thread_count(size_t file_size)
{
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    if (cpu_count < 1)
        cpu_count = 1;

    if (cpu_count > MAX_THREADS)
        cpu_count = MAX_THREADS;

    size_t by_size = file_size / (8u * 1024u * 1024u);
    if (by_size < 1)
        by_size = 1;

    if ((size_t)cpu_count > by_size)
        cpu_count = (long)by_size;

    return (unsigned)cpu_count;
}

static int compute_results_file(const char *filename, char **output_ptr, size_t *output_size_ptr)
{
    if (!output_ptr || !output_size_ptr)
        return 1;

    *output_ptr = NULL;
    *output_size_ptr = 0;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return 1;
    }

    size_t size = (size_t)st.st_size;
    if (size == 0) {
        *output_ptr = malloc(4);
        if (!*output_ptr) {
            perror("malloc");
            close(fd);
            return 1;
        }

        memcpy(*output_ptr, "{}\n", 3);
        (*output_ptr)[3] = '\0';
        *output_size_ptr = 3;
        close(fd);
        return 0;
    }

    const char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

#if defined(MADV_SEQUENTIAL)
    madvise((void *)data, size, MADV_SEQUENTIAL);
#endif

#if defined(MADV_WILLNEED)
    madvise((void *)data, size, MADV_WILLNEED);
#endif

    unsigned thread_count = detect_thread_count(size);
    Worker *workers = NULL;
    pthread_t *threads = NULL;

    if (posix_memalign((void **)&workers, 64, thread_count * sizeof(*workers)) != 0) {
        perror("posix_memalign");
        munmap((void *)data, size);
        close(fd);
        return 1;
    }

    threads = malloc(thread_count * sizeof(*threads));
    if (!threads) {
        perror("malloc");
        free(workers);
        munmap((void *)data, size);
        close(fd);
        return 1;
    }

    memset(workers, 0, thread_count * sizeof(*workers));

    for (unsigned i = 0; i < thread_count; ++i) {
        size_t raw_start = ((uint64_t)size * i) / thread_count;
        size_t raw_end = ((uint64_t)size * (i + 1)) / thread_count;

        workers[i].start = i == 0 ? data : next_record_boundary(data, size, raw_start);
        workers[i].end = i + 1 == thread_count ? data + size : next_record_boundary(data, size, raw_end);
        workers[i].thread_id = i;

        if (posix_memalign((void **)&workers[i].table, 64, LOCAL_TABLE_SIZE * sizeof(Entry)) != 0) {
            perror("posix_memalign");
            for (unsigned j = 0; j < i; ++j)
                free(workers[j].table);
            free(threads);
            free(workers);
            munmap((void *)data, size);
            close(fd);
            return 1;
        }

        memset(workers[i].table, 0, LOCAL_TABLE_SIZE * sizeof(Entry));
    }

    for (unsigned i = 1; i < thread_count; ++i) {
        int rc = pthread_create(&threads[i], NULL, worker_main, &workers[i]);
        if (rc != 0) {
            errno = rc;
            perror("pthread_create");
            free(threads);
            free(workers);
            munmap((void *)data, size);
            close(fd);
            return 1;
        }
    }

    worker_main(&workers[0]);

    for (unsigned i = 1; i < thread_count; ++i)
        pthread_join(threads[i], NULL);

    Entry *final_table;
    if (posix_memalign((void **)&final_table, 64, FINAL_TABLE_SIZE * sizeof(Entry)) != 0) {
        perror("posix_memalign");
        free(threads);
        free(workers);
        munmap((void *)data, size);
        close(fd);
        return 1;
    }

    memset(final_table, 0, FINAL_TABLE_SIZE * sizeof(Entry));

    for (unsigned t = 0; t < thread_count; ++t) {
        Entry *local = workers[t].table;
        for (uint32_t i = 0; i < LOCAL_TABLE_SIZE; ++i) {
            if (local[i].hash)
                merge_entry(final_table, &local[i]);
        }
    }

    Entry **sorted = malloc(10000 * sizeof(*sorted));
    if (!sorted) {
        perror("malloc");
        free(final_table);
        free(threads);
        free(workers);
        munmap((void *)data, size);
        close(fd);
        return 1;
    }

    size_t station_count = 0;
    for (uint32_t i = 0; i < FINAL_TABLE_SIZE; ++i) {
        if (final_table[i].hash)
            sorted[station_count++] = &final_table[i];
    }

    qsort(sorted, station_count, sizeof(*sorted), entry_cmp);

    size_t output_capacity = station_count * 160 + 4;
    char *output = malloc(output_capacity);
    if (!output) {
        perror("malloc");
        free(sorted);
        free(final_table);
        free(threads);
        free(workers);
        munmap((void *)data, size);
        close(fd);
        return 1;
    }

    char *out = output;
    *out++ = '{';

    for (size_t i = 0; i < station_count; ++i) {
        Entry *e = sorted[i];

        if (i)
            *out++ = ',';

        memcpy(out, e->name, e->len);
        out += e->len;

        *out++ = '=';
        out = append_tenth(out, e->min);
        *out++ = '/';
        out = append_tenth(out, rounded_average(e->sum, e->count));
        *out++ = '/';
        out = append_tenth(out, e->max);
    }

    *out++ = '}';
    *out++ = '\n';

    *output_ptr = output;
    *output_size_ptr = (size_t)(out - output);

    free(sorted);
    free(final_table);

    for (unsigned i = 0; i < thread_count; ++i)
        free(workers[i].table);

    free(threads);
    free(workers);
    munmap((void *)data, size);
    close(fd);

    return 0;
}

static PyObject *brc_calculate(PyObject *self, PyObject *args)
{
    const char *filename;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return NULL;

    char *output = NULL;
    size_t output_size = 0;

    if (compute_results_file(filename, &output, &output_size) != 0)
        return NULL;

    PyObject *result = PyUnicode_FromStringAndSize(output, (Py_ssize_t)output_size);
    free(output);
    return result;
}

static PyMethodDef brc_methods[] = {
    {"calculate", brc_calculate, METH_VARARGS, "Compute the 1BRC results for a file and return them as a Python string."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef brcmodule = {
    PyModuleDef_HEAD_INIT,
    "brc",
    "CPython extension for OBL challenge aggregation",
    -1,
    brc_methods,
};

PyMODINIT_FUNC PyInit_brc(void)
{
    return PyModule_Create(&brcmodule);
}

int main(int argc, char **argv)
{
    const char *filename = argc > 1 ? argv[1] : "measurements.txt";
    char *output = NULL;
    size_t output_size = 0;

    if (compute_results_file(filename, &output, &output_size) != 0)
        return 1;

    size_t written = 0;
    while (written < output_size) {
        ssize_t n = write(STDOUT_FILENO, output + written, output_size - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            free(output);
            return 1;
        }
        written += (size_t)n;
    }

    free(output);
    return 0;
}
