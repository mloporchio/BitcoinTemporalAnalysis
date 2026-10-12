// dmax_chunk_distribution.cpp
//
// Input : two binary files with N elements each (native byte order, no header):
//           timestamps.bin  N x uint32  creation UNIX timestamp (seconds)
//           dmax.bin        N x uint32  d_max of the same output
//         (element i of both files refers to the same output).
// Task  : split outputs into 6-month chunks by creation timestamp, from
//         2009-01-01 00:00:00 UTC up to 2022-12-31 23:59:59 UTC (28 chunks:
//         [Jan 1, Jul 1) and [Jul 1, Jan 1) of every year 2009..2022), and
//         compute for every chunk the distribution of d_max.
// Output: one binary file per chunk, <outPrefix>_<YYYY>_H<1|2>.bin, containing
//         packed 12-byte records (native byte order, no padding), sorted by
//         d_max ascending:
//             uint32 d_max, uint64 frequency
//         numpy: np.fromfile(f, dtype=np.dtype([('d_max','<u4'),('freq','<u8')]))
//         All 28 files are always written (empty file if the chunk has no rows).
//         Outputs outside the 28 chunks are counted and reported, not written.
//
// Method: a single sequential pass over both files. Per chunk, a dense
// counter array indexed by d_max (grown on demand, up to DENSE_LIMIT) plus a
// hash-map fallback for rare huge d_max values. The chunk lookup first
// checks the chunk of the previous row (timestamps are almost sorted in
// practice), then falls back to a binary search over the 29 boundaries.
//
// Compilation:
//   g++ -O3 -march=native -std=c++17 -o dmax_chunk_distribution dmax_chunk_distribution.cpp
// Usage:
//   ./dmax_chunk_distribution <timestamps.bin> <dmax.bin> <outPrefix>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static constexpr int FIRST_YEAR = 2009, LAST_YEAR = 2022;
static constexpr int NUM_CHUNKS = (LAST_YEAR - FIRST_YEAR + 1) * 2;     // 28
static constexpr size_t BLOCK_ELEMS = 8u << 20;                          // 8M elements per file per read (32 MiB)
static constexpr uint32_t DENSE_LIMIT = 1u << 22;                        // dense array up to d_max < 4M

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

struct Chunk {
    std::vector<uint64_t> dense;
    std::unordered_map<uint32_t, uint64_t> sparse;
    uint64_t rows = 0;
    inline void add(uint32_t d) {
        ++rows;
        if (d < DENSE_LIMIT) {
            if (d >= dense.size()) dense.resize(std::max<size_t>(d + 1, dense.size() * 2), 0);
            ++dense[d];
        } else {
            ++sparse[d];
        }
    }
};

static bool readFull(int fd, void* buf, size_t bytes, size_t& got) {
    char* p = static_cast<char*>(buf);
    got = 0;
    while (got < bytes) {
        ssize_t n = ::read(fd, p + got, bytes - got);
        if (n < 0) { perror("read"); std::exit(1); }
        if (n == 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

static int64_t fileSize(const char* path) {
    struct stat st;
    if (::stat(path, &st) != 0) { perror(path); std::exit(1); }
    return st.st_size;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: %s <timestamps.bin> <dmax.bin> <outPrefix>\n", argv[0]);
        return 1;
    }
    const char* tsPath = argv[1];
    const char* dmPath = argv[2];
    const std::string prefix = argv[3];

    const int64_t szT = fileSize(tsPath), szD = fileSize(dmPath);
    if (szT != szD || szT % 4 != 0) {
        std::fprintf(stderr, "Size mismatch or not a multiple of 4: %s=%" PRId64 " %s=%" PRId64 "\n",
                     tsPath, szT, dmPath, szD);
        return 1;
    }
    const uint64_t N = static_cast<uint64_t>(szT) / 4;

    // bounds[c] = start of chunk c (UTC); bounds[NUM_CHUNKS] = 2023-01-01.
    int64_t bounds[NUM_CHUNKS + 1];
    for (int c = 0; c <= NUM_CHUNKS; ++c) {
        const int year = FIRST_YEAR + c / 2;
        const unsigned month = (c % 2 == 0) ? 1 : 7;
        bounds[c] = daysFromCivil(year, month, 1) * 86400;
    }

    int fdT = ::open(tsPath, O_RDONLY), fdD = ::open(dmPath, O_RDONLY);
    if (fdT < 0) { perror(tsPath); return 1; }
    if (fdD < 0) { perror(dmPath); return 1; }
#ifdef POSIX_FADV_SEQUENTIAL
    ::posix_fadvise(fdT, 0, 0, POSIX_FADV_SEQUENTIAL);
    ::posix_fadvise(fdD, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    std::vector<uint32_t> tsBuf(BLOCK_ELEMS), dmBuf(BLOCK_ELEMS);
    std::vector<Chunk> chunks(NUM_CHUNKS);
    uint64_t outside = 0, processed = 0;

    const auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - t0).count());
    };

    int cur = -1;                 // chunk of the previous row (-1 = none)
    int64_t curLo = 0, curHi = 0; // its [lo, hi) range
    uint64_t nextReport = 500000000;

    while (processed < N) {
        const size_t want = static_cast<size_t>(std::min<uint64_t>(BLOCK_ELEMS, N - processed));
        size_t g1, g2;
        readFull(fdT, tsBuf.data(), want * 4, g1);
        readFull(fdD, dmBuf.data(), want * 4, g2);
        if (g1 != want * 4 || g2 != want * 4) { std::fprintf(stderr, "Unexpected EOF.\n"); return 1; }

        for (size_t i = 0; i < want; ++i) {
            const int64_t ts = tsBuf[i];
            if (ts < curLo || ts >= curHi || cur < 0) {
                if (ts < bounds[0] || ts >= bounds[NUM_CHUNKS]) { ++outside; cur = -1; curLo = curHi = 0; continue; }
                cur = static_cast<int>(std::upper_bound(bounds, bounds + NUM_CHUNKS + 1, ts) - bounds) - 1;
                curLo = bounds[cur];
                curHi = bounds[cur + 1];
            }
            chunks[cur].add(dmBuf[i]);
        }
        processed += want;
        if (processed >= nextReport) {
            std::printf("Processed %" PRIu64 " / %" PRIu64 " rows (%lld s)\n", processed, N, secs());
            std::fflush(stdout);
            nextReport += 500000000;
        }
    }
    ::close(fdT);
    ::close(fdD);

    // Write one file per chunk.
    #pragma pack(push, 1)
    struct Rec { uint32_t d; uint64_t n; };
    #pragma pack(pop)
    static_assert(sizeof(Rec) == 12, "Rec must be 12 bytes");

    for (int c = 0; c < NUM_CHUNKS; ++c) {
        char name[64];
        std::snprintf(name, sizeof(name), "_%d_H%d.bin", FIRST_YEAR + c / 2, c % 2 + 1);
        const std::string path = prefix + name;

        std::vector<Rec> recs;
        const Chunk& ch = chunks[c];
        for (size_t d = 0; d < ch.dense.size(); ++d)
            if (ch.dense[d]) recs.push_back({static_cast<uint32_t>(d), ch.dense[d]});
        const size_t denseEnd = recs.size();
        for (const auto& kv : ch.sparse) recs.push_back({kv.first, kv.second});
        std::sort(recs.begin() + static_cast<std::ptrdiff_t>(denseEnd), recs.end(),
                  [](const Rec& a, const Rec& b) { return a.d < b.d; });

        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { perror(path.c_str()); return 1; }
        if (!recs.empty() && std::fwrite(recs.data(), sizeof(Rec), recs.size(), f) != recs.size()) {
            perror("fwrite"); return 1;
        }
        std::fclose(f);
        std::printf("%s: %" PRIu64 " rows, %zu distinct d_max\n", path.c_str(), ch.rows, recs.size());
    }
    std::printf("Done: %" PRIu64 " rows read, %" PRIu64 " outside [2009-01-01, 2023-01-01) ignored (%lld s).\n",
                N, outside, secs());
    return 0;
}
