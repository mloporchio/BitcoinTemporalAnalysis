// creation_block_extract.cpp
//
// Reads the original Bitcoin transaction list (text) and writes, for every
// transaction output, the timestamp of the block in which it was created
// (Unix time in seconds, the first field of the `info` section).
//
// Output format: a flat binary file containing one u32 per output (4
// bytes, native byte order, same convention as the rest of this
// pipeline), with no header and no padding. Outputs appear in order of
// increasing tx_id and, for a given tx_id, in order of increasing offset:
// this is exactly the record order of utxo_extract's output, so row i of
// this file refers to the same output as record i of utxo_extract's file
// (given the same input files, in the same order).
//
// Duplicate transactions (same tx_id seen more than once, in the same file
// or across files) are handled as in utxo_extract: only the first
// occurrence is kept, and the outputs of every later occurrence are
// ignored, so each output is counted exactly once.
//
// Input line format:
//   info:input;input;...:output;output;...
//   info = timestamp,block_id,tx_id,...
// Only timestamp, tx_id and the *number* of outputs are needed here, so the
// input section of each line is never scanned at all: the second ':'
// separator is located by searching backwards from the end of the line
// (memrchr), which only touches the (usually short) output section.
//
// Memory: since every output of a transaction was created in the same
// block, there is no need for a per-output array. One 8-byte record per
// tx_id (timestamp, number of outputs) is enough, i.e. 8*M bytes for tx_id
// values in [0, M): well below the per-output arrays of utxo_extract. The
// final pass expands these records into the per-output file in tx_id
// order, so no sort is needed.
//
// Compilation:
//   g++ -O3 -march=native -std=c++17 -o creation_block_extract creation_block_extract.cpp
//
// Usage:
//   ./creation_block_extract <inputFile1> [inputFile2 ...] <outputFile>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <cerrno>
#include <algorithm>
#include <string>
#include <vector>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>

// Marks a tx_id never encountered: stored in the `cnt` field, since a real
// transaction can never have 2^32-1 outputs (any u32 is a valid timestamp,
// so the sentinel cannot live in the timestamp field).
static constexpr uint32_t NOT_SEEN = 0xFFFFFFFFu;

static constexpr size_t READ_BUFFER_SIZE  = 64u << 20;   // 64 MiB
static constexpr size_t WRITE_BUFFER_SIZE = 32u << 20;   // 32 MiB
static constexpr size_t MAX_LINE_SIZE     = 16u << 20;

struct TxInfo {
    uint32_t timestamp; // timestamp of the creation block (Unix seconds)
    uint32_t cnt;       // number of outputs (NOT_SEEN if tx_id never seen)
};
static_assert(sizeof(TxInfo) == 8, "TxInfo must be exactly 8 bytes");

// ---------------------------------------------------------------------------
// Sequential multi-file line reader (same as in utxo_extract): files are
// read one after another as a single stream; the last line of a file is
// returned whole even without a trailing '\n'.
// ---------------------------------------------------------------------------

class LineReader {
public:
    explicit LineReader(std::vector<std::string> paths) : paths_(std::move(paths)) {
        buf_.resize(READ_BUFFER_SIZE);
        openNext();
    }

    ~LineReader() { if (fd_ >= 0) ::close(fd_); }

    bool next(const char*& begin, const char*& end) {
        for (;;) {
            if (pos_ < len_) {
                const char* start = buf_.data() + pos_;
                const char* nl = static_cast<const char*>(
                    ::memchr(start, '\n', len_ - pos_));
                if (nl) {
                    begin = start;
                    end   = nl;
                    if (end > begin && end[-1] == '\r') --end;
                    pos_ += static_cast<size_t>(nl - start) + 1;
                    return true;
                }
            }
            if (eof_) {
                if (pos_ < len_) {
                    begin = buf_.data() + pos_;
                    end   = buf_.data() + len_;
                    if (end > begin && end[-1] == '\r') --end;
                    pos_ = len_;
                    return true;
                }
                if (!openNext()) return false;
                continue;
            }
            refill();
        }
    }

private:
    bool openNext() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        if (fileIndex_ >= paths_.size()) return false;
        const std::string& path = paths_[fileIndex_++];
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) {
            std::fprintf(stderr, "open(%s): %s\n", path.c_str(), std::strerror(errno));
            std::exit(1);
        }
#ifdef POSIX_FADV_SEQUENTIAL
        ::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
        ::posix_fadvise(fd_, 0, 0, POSIX_FADV_NOREUSE);
#endif
        pos_ = len_ = 0;
        eof_ = false;
        std::printf("Reading %s...\n", path.c_str());
        std::fflush(stdout);
        return true;
    }

    void refill() {
        size_t remaining = len_ - pos_;
        if (remaining > 0 && pos_ > 0)
            ::memmove(buf_.data(), buf_.data() + pos_, remaining);
        pos_ = 0;
        len_ = remaining;

        if (len_ == buf_.size()) {
            if (buf_.size() >= MAX_LINE_SIZE) {
                std::fprintf(stderr, "Line too long (> %zu bytes).\n", buf_.size());
                std::exit(1);
            }
            buf_.resize(buf_.size() * 2);
        }

        ssize_t n = ::read(fd_, buf_.data() + len_, buf_.size() - len_);
        if (n < 0) { perror("read(input)"); std::exit(1); }
        if (n == 0) eof_ = true;
        else len_ += static_cast<size_t>(n);
    }

    std::vector<std::string> paths_;
    size_t fileIndex_ = 0;
    int fd_ = -1;
    std::vector<char> buf_;
    size_t pos_ = 0, len_ = 0;
    bool eof_ = false;
};

// ---------------------------------------------------------------------------
// Buffered writer of u32 values, with a bulk "repeat this value N times"
// operation (every output of a transaction has the same creation timestamp).
// ---------------------------------------------------------------------------

class BlockWriter {
public:
    explicit BlockWriter(const char* path) {
        fd_ = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) { perror("open(output)"); std::exit(1); }
        buf_.resize(WRITE_BUFFER_SIZE / sizeof(uint32_t));
    }

    ~BlockWriter() { flush(); if (fd_ >= 0) ::close(fd_); }

    void writeRepeated(uint32_t value, uint32_t count) {
        while (count > 0) {
            if (pos_ == buf_.size()) flush();
            const size_t room = buf_.size() - pos_;
            const size_t n = count < room ? count : room;
            std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(pos_),
                      buf_.begin() + static_cast<std::ptrdiff_t>(pos_ + n), value);
            pos_ += n;
            count -= static_cast<uint32_t>(n);
        }
    }

    void flush() {
        const char* p = reinterpret_cast<const char*>(buf_.data());
        const size_t bytes = pos_ * sizeof(uint32_t);
        size_t off = 0;
        while (off < bytes) {
            ssize_t n = ::write(fd_, p + off, bytes - off);
            if (n <= 0) { perror("write(output)"); std::exit(1); }
            off += static_cast<size_t>(n);
        }
        pos_ = 0;
    }

private:
    int fd_ = -1;
    std::vector<uint32_t> buf_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

static inline int64_t parseInt(const char*& p, const char* e) {
    bool neg = false;
    if (p < e && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
    int64_t v = 0;
    while (p < e && static_cast<unsigned>(*p - '0') < 10u) {
        v = v * 10 + (*p - '0');
        ++p;
    }
    return neg ? -v : v;
}

// Number of fields separated by `sep`, with the same semantics as Java's
// String.split() (trailing empty fields are discarded; an empty string
// has zero fields here, since an empty output section means no outputs).
static inline uint32_t countFields(const char* s, const char* e, char sep) {
    while (e > s && e[-1] == sep) --e;
    if (s == e) return 0;
    uint32_t n = 1;
    const char* p = s;
    size_t left = static_cast<size_t>(e - s);
    while (left) {
        const char* f = static_cast<const char*>(::memchr(p, sep, left));
        if (!f) break;
        ++n;
        left -= static_cast<size_t>(f - p) + 1;
        p = f + 1;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <inputFile1> [inputFile2 ...] <outputFile>\n", argv[0]);
        return 1;
    }
    const char* outputFile = argv[argc - 1];
    std::vector<std::string> inputFiles;
    inputFiles.reserve(static_cast<size_t>(argc - 2));
    for (int i = 1; i < argc - 1; ++i) inputFiles.emplace_back(argv[i]);

    LineReader in(std::move(inputFiles));
    BlockWriter out(outputFile);

    // One record per tx_id, indexed directly by tx_id (dense array).
    std::vector<TxInfo> infos;
    infos.reserve(1u << 20);

    uint64_t txCount = 0, outputCount = 0, duplicateCount = 0;
    int64_t maxTxId = -1;

    const auto startTime = std::chrono::steady_clock::now();
    auto seconds = [&]() {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count());
    };

    const char* lb;
    const char* le;
    while (in.next(lb, le)) {
        if (lb == le) continue;

        // Neither ':' separator is searched from the left: the second one
        // is found backwards from the end of the line (this only crosses
        // the output section), and the first one forwards from the start
        // of the line, stopping at the second (this only crosses the info
        // section). The input section is never read.
        const char* c2 = static_cast<const char*>(
            ::memrchr(lb, ':', static_cast<size_t>(le - lb)));
        if (!c2) continue;
        const char* c1 = static_cast<const char*>(
            ::memchr(lb, ':', static_cast<size_t>(c2 - lb)));
        if (!c1) continue;

        // info = timestamp,block_id,tx_id,... : parse the timestamp, skip
        // block_id, parse tx_id.
        const char* p = lb;
        const int64_t ts = parseInt(p, c1);
        if (ts < 0 || ts > static_cast<int64_t>(UINT32_MAX)) {
            std::fprintf(stderr, "Timestamp %" PRId64 " does not fit in a u32.\n", ts);
            return 1;
        }
        p = static_cast<const char*>(::memchr(p, ',', static_cast<size_t>(c1 - p)));
        if (!p) continue;
        ++p;
        (void)parseInt(p, c1);               // block_id (not needed)
        if (p < c1 && *p == ',') ++p;
        const int64_t txId = parseInt(p, c1);
        if (txId < 0) continue;
        const uint32_t timestamp = static_cast<uint32_t>(ts);

        // Duplicate transaction: keep only the first occurrence.
        if (static_cast<size_t>(txId) < infos.size() &&
            infos[static_cast<size_t>(txId)].cnt != NOT_SEEN) {
            ++duplicateCount;
            continue;
        }

        const uint32_t outCount = countFields(c2 + 1, le, ';');

        if (static_cast<size_t>(txId) >= infos.size()) {
            size_t newSize = static_cast<size_t>(txId) + 1;
            if (newSize < infos.size() * 2) newSize = infos.size() * 2;
            infos.resize(newSize, TxInfo{0, NOT_SEEN});
        }
        infos[static_cast<size_t>(txId)] = TxInfo{timestamp, outCount};
        if (txId > maxTxId) maxTxId = txId;

        outputCount += outCount;
        ++txCount;
        if (txCount % 10000000 == 0) {
            std::printf("Processed: %" PRIu64 " transactions, %" PRIu64
                        " outputs so far (after %lld seconds).\n",
                        txCount, outputCount, seconds());
            std::fflush(stdout);
        }
    }

    // Final pass: expand the per-transaction records into one u32 per
    // output, in tx_id order (so no sort is needed).
    std::printf("Writing %" PRIu64 " creation timestamps...\n", outputCount);
    std::fflush(stdout);
    uint64_t written = 0;
    for (int64_t txId = 0; txId <= maxTxId; ++txId) {
        const TxInfo& t = infos[static_cast<size_t>(txId)];
        if (t.cnt == NOT_SEEN || t.cnt == 0) continue;
        out.writeRepeated(t.timestamp, t.cnt);
        written += t.cnt;
    }
    out.flush();

    std::printf("Done: %" PRIu64 " transactions, %" PRIu64 " outputs written (%lld seconds).\n",
                txCount, written, seconds());
    if (duplicateCount)
        std::printf("Duplicate transactions ignored: %" PRIu64 "\n", duplicateCount);
    return 0;
}
