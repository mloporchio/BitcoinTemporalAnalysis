// edge_sorter.cpp
//
// Sorter in memoria esterna per la edge list binaria del Bitcoin Payment
// Graph. Equivalente a:
//
//     LC_ALL=C sort -k1,1n -k2,2n -u edges.txt
//
// ma su un file binario di coppie (source, target) di interi a 64 bit senza
// segno, in byte order nativo della macchina (16 byte per arco, nessun
// header). L'ordinamento e' lessicografico sulla coppia: prima per source, a
// parita' di source per target. L'output ha lo stesso formato dell'input.
//
// ATTENZIONE: sia il file di input che quello di output sono nel byte order
// nativo della macchina che esegue il programma: non viene fatta nessuna
// conversione. Se l'input e' stato prodotto da una macchina con byte order
// diverso (o e' in un formato esplicito come big-endian), i risultati non
// sono validi.
//
// Algoritmo: external merge sort a piu' passate.
//   Fase 1 (run formation) - il file viene letto in blocchi grandi quanto la
//     memoria concessa; ogni blocco viene ordinato in parallelo e scritto su
//     una "run" temporanea gia' deduplicata.
//   Fase 2 (merge) - le run vengono fuse k alla volta con un heap; se le run
//     sono piu' del fan-in massimo si eseguono piu' passate. L'ultima passata
//     scrive direttamente il file di output.
//
// Tutti i file, temporanei e finale, condividono lo stesso formato: nessuna
// conversione avviene in nessuna fase.
//
// Compilazione:
//   g++ -O3 -march=native -std=c++17 -pthread -o edge_sorter edge_sorter.cpp
//
// Uso:
//   ./edge_sorter <input.bin> <output.bin> [opzioni]
//
// Opzioni:
//   -m <MiB>    memoria di lavoro, default 4096
//   -t <n>      thread per l'ordinamento in memoria, default = numero di core
//   -T <dir>    directory dei file temporanei, default "."
//   -k <n>      fan-in massimo del merge, default 64
//   -D          NON deduplicare (mantiene gli archi ripetuti)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Tipi di base
// ---------------------------------------------------------------------------

struct Edge {
    uint64_t s;
    uint64_t t;
};
static_assert(sizeof(Edge) == 16, "Edge deve occupare esattamente 16 byte");

static inline bool operator<(const Edge& a, const Edge& b) {
    return (a.s != b.s) ? (a.s < b.s) : (a.t < b.t);
}
static inline bool operator==(const Edge& a, const Edge& b) {
    return a.s == b.s && a.t == b.t;
}

static void fail(const char* what) { perror(what); std::exit(1); }

// ---------------------------------------------------------------------------
// Scrittore bufferizzato di record Edge
// ---------------------------------------------------------------------------

class EdgeSink {
public:
    EdgeSink(const std::string& path, size_t bufBytes) {
        fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) fail("open(output)");
        buf_.resize(bufBytes / sizeof(Edge));
    }

    ~EdgeSink() { close(); }

    inline void push(const Edge& e) {
        if (__builtin_expect(pos_ == buf_.size(), 0)) flush();
        buf_[pos_++] = e;
    }

    void flush() {
        const char* p = reinterpret_cast<const char*>(buf_.data());
        size_t bytes = pos_ * sizeof(Edge), off = 0;
        while (off < bytes) {
            ssize_t n = ::write(fd_, p + off, bytes - off);
            if (n <= 0) fail("write(output)");
            off += static_cast<size_t>(n);
        }
        pos_ = 0;
    }

    void close() {
        if (fd_ >= 0) { flush(); ::close(fd_); fd_ = -1; }
    }

private:
    int fd_ = -1;
    std::vector<Edge> buf_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Sorgente: file di record Edge in formato nativo
// ---------------------------------------------------------------------------

class FileSource {
public:
    FileSource(const std::string& path, size_t bufBytes, bool unlinkOnClose)
        : path_(path), unlink_(unlinkOnClose) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) fail("open(run)");
#ifdef POSIX_FADV_SEQUENTIAL
        ::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
        buf_.resize(std::max<size_t>(bufBytes / sizeof(Edge), 1024));
        refill();
    }

    FileSource(FileSource&& o) noexcept { moveFrom(std::move(o)); }
    FileSource& operator=(FileSource&& o) noexcept {
        if (this != &o) { release(); moveFrom(std::move(o)); }
        return *this;
    }
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    ~FileSource() { release(); }

    inline bool empty() const { return pos_ == len_; }
    inline const Edge& top() const { return buf_[pos_]; }
    inline void pop() { if (++pos_ == len_) refill(); }

private:
    void moveFrom(FileSource&& o) {
        fd_ = o.fd_; o.fd_ = -1;
        buf_ = std::move(o.buf_);
        pos_ = o.pos_; len_ = o.len_;
        path_ = std::move(o.path_); unlink_ = o.unlink_;
    }

    void release() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; if (unlink_) ::unlink(path_.c_str()); }
    }

    void refill() {
        pos_ = 0;
        len_ = 0;
        char* p = reinterpret_cast<char*>(buf_.data());
        size_t capacity = buf_.size() * sizeof(Edge), got = 0;
        while (got < capacity) {
            ssize_t n = ::read(fd_, p + got, capacity - got);
            if (n < 0) fail("read(run)");
            if (n == 0) break;
            got += static_cast<size_t>(n);
        }
        len_ = got / sizeof(Edge);
    }

    int fd_ = -1;
    std::vector<Edge> buf_;
    size_t pos_ = 0, len_ = 0;
    std::string path_;
    bool unlink_ = false;
};

// Sorgente: blocco gia' ordinato in memoria.
class MemSource {
public:
    MemSource(const Edge* b, const Edge* e) : p_(b), e_(e) {}
    inline bool empty() const { return p_ == e_; }
    inline const Edge& top() const { return *p_; }
    inline void pop() { ++p_; }
private:
    const Edge* p_;
    const Edge* e_;
};

// ---------------------------------------------------------------------------
// Merge k-vie con heap sugli indici delle sorgenti
// ---------------------------------------------------------------------------

template <class Src>
static uint64_t kwayMerge(std::vector<Src>& srcs, EdgeSink& out, bool dedup) {
    std::vector<uint32_t> heap;
    heap.reserve(srcs.size());
    for (uint32_t i = 0; i < srcs.size(); ++i)
        if (!srcs[i].empty()) heap.push_back(i);

    auto greater = [&srcs](uint32_t a, uint32_t b) { return srcs[b].top() < srcs[a].top(); };
    std::make_heap(heap.begin(), heap.end(), greater);

    uint64_t emitted = 0;
    Edge last{0, 0};
    bool hasLast = false;

    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), greater);
        const uint32_t i = heap.back();
        const Edge e = srcs[i].top();

        if (!dedup || !hasLast || !(e == last)) {
            out.push(e);
            ++emitted;
            last = e;
            hasLast = true;
        }

        srcs[i].pop();
        if (srcs[i].empty()) heap.pop_back();
        else std::push_heap(heap.begin(), heap.end(), greater);
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// Ordinamento in memoria multi-thread
// ---------------------------------------------------------------------------

// Ordina [begin, begin+n) dividendolo in `threads` blocchi ordinati in
// parallelo. Restituisce i confini dei blocchi: la fusione non avviene in
// memoria (richiederebbe un buffer ausiliario grande quanto il blocco) ma
// viene delegata al merge k-vie che scrive la run su disco.
static std::vector<size_t> parallelBlockSort(Edge* begin, size_t n, int threads) {
    if (threads < 1) threads = 1;
    if (n < (1u << 16)) threads = 1;
    if (static_cast<size_t>(threads) > n) threads = static_cast<int>(n ? n : 1);

    std::vector<size_t> bounds(static_cast<size_t>(threads) + 1);
    for (int i = 0; i <= threads; ++i)
        bounds[static_cast<size_t>(i)] = n * static_cast<size_t>(i) / static_cast<size_t>(threads);

    if (threads == 1) {
        std::sort(begin, begin + n);
        return bounds;
    }

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int i = 0; i < threads; ++i) {
        const size_t lo = bounds[static_cast<size_t>(i)];
        const size_t hi = bounds[static_cast<size_t>(i) + 1];
        workers.emplace_back([begin, lo, hi] { std::sort(begin + lo, begin + hi); });
    }
    for (auto& w : workers) w.join();
    return bounds;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "Usage: %s <input.bin> <output.bin> [-m MiB] [-t threads] [-T tmpdir] [-k fanin] [-D]\n",
            argv[0]);
        return 1;
    }
    const std::string inputFile  = argv[1];
    const std::string outputFile = argv[2];

    size_t memoryMiB = 4096;
    int threads      = static_cast<int>(std::thread::hardware_concurrency());
    if (threads < 1) threads = 1;
    std::string tmpDir = ".";
    size_t fanIn = 64;
    bool dedup = true;

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-m" && i + 1 < argc) memoryMiB = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "-t" && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (a == "-T" && i + 1 < argc) tmpDir = argv[++i];
        else if (a == "-k" && i + 1 < argc) fanIn = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "-D") dedup = false;
        else { std::fprintf(stderr, "Opzione sconosciuta: %s\n", a.c_str()); return 1; }
    }
    if (threads < 1) threads = 1;
    if (fanIn < 2) fanIn = 2;

    struct stat st;
    if (::stat(inputFile.c_str(), &st) != 0) fail("stat(input)");
    if (st.st_size % static_cast<off_t>(sizeof(Edge)) != 0) {
        std::fprintf(stderr, "La dimensione dell'input non e' un multiplo di 16 byte.\n");
        return 1;
    }
    const uint64_t totalRecords = static_cast<uint64_t>(st.st_size) / sizeof(Edge);

    const auto startTime = std::chrono::steady_clock::now();
    auto seconds = [&] {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count());
    };

    const size_t memoryBytes = memoryMiB * 1024u * 1024u;
    // Non ha senso allocare piu' memoria di quanti record ci siano.
    size_t chunkRecords = std::max<size_t>(memoryBytes / sizeof(Edge), 1u << 16);
    if (totalRecords > 0 && chunkRecords > totalRecords)
        chunkRecords = static_cast<size_t>(totalRecords);

    if (totalRecords == 0) {
        EdgeSink sink(outputFile, 1u << 20);
        std::printf("Input vuoto.\n");
        return 0;
    }

    std::printf("Records: %" PRIu64 " (%.2f GiB), memoria: %zu MiB, thread: %d\n",
                totalRecords, static_cast<double>(st.st_size) / (1024.0 * 1024.0 * 1024.0),
                memoryMiB, threads);

    // -----------------------------------------------------------------------
    // Fase 1: formazione delle run ordinate
    // -----------------------------------------------------------------------

    const std::string prefix = tmpDir + "/edgesort_" + std::to_string(::getpid()) + "_";
    std::vector<std::string> runs;
    uint64_t runRecords = 0;

    {
        int fd = ::open(inputFile.c_str(), O_RDONLY);
        if (fd < 0) fail("open(input)");
#ifdef POSIX_FADV_SEQUENTIAL
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);
#endif
        std::vector<Edge> chunk(chunkRecords);
        uint64_t readRecords = 0;

        for (;;) {
            char* p = reinterpret_cast<char*>(chunk.data());
            const size_t capacity = chunkRecords * sizeof(Edge);
            size_t got = 0;
            while (got < capacity) {
                ssize_t n = ::read(fd, p + got, capacity - got);
                if (n < 0) fail("read(input)");
                if (n == 0) break;
                got += static_cast<size_t>(n);
            }
            const size_t n = got / sizeof(Edge);
            if (n == 0) break;
            readRecords += n;

            const std::vector<size_t> bounds = parallelBlockSort(chunk.data(), n, threads);

            const bool lastRun = (readRecords == totalRecords) && runs.empty();
            const std::string path = lastRun ? outputFile
                                             : prefix + "run" + std::to_string(runs.size());

            {
                EdgeSink sink(path, 8u << 20);
                std::vector<MemSource> blocks;
                blocks.reserve(bounds.size() - 1);
                for (size_t b = 0; b + 1 < bounds.size(); ++b)
                    blocks.emplace_back(chunk.data() + bounds[b], chunk.data() + bounds[b + 1]);
                runRecords = kwayMerge(blocks, sink, dedup);
            }

            if (lastRun) {
                std::printf("Tutto in memoria: %" PRIu64 " archi ordinati in %lld secondi.\n",
                            runRecords, seconds());
                ::close(fd);
                return 0;
            }

            runs.push_back(path);
            std::printf("Run %zu scritta (%" PRIu64 "/%" PRIu64 " record letti, %lld s).\n",
                        runs.size(), readRecords, totalRecords, seconds());
            std::fflush(stdout);

            if (got < capacity) break;   // fine del file
        }
        ::close(fd);
    }

    if (runs.empty()) {   // input vuoto
        EdgeSink sink(outputFile, 1u << 20);
        std::printf("Input vuoto.\n");
        return 0;
    }

    // -----------------------------------------------------------------------
    // Fase 2: merge a piu' passate
    // -----------------------------------------------------------------------

    // Memoria divisa fra le sorgenti aperte contemporaneamente, piu' l'output.
    size_t level = 0;
    uint64_t finalRecords = 0;

    while (runs.size() > 1) {
        const bool finalPass = (runs.size() <= fanIn);
        std::vector<std::string> nextRuns;
        size_t index = 0;

        for (size_t i = 0; i < runs.size(); i += fanIn) {
            const size_t hi = std::min(runs.size(), i + fanIn);
            const size_t group = hi - i;
            const size_t perSource = std::max<size_t>(memoryBytes / (group + 2), 1u << 20);

            const std::string outPath = finalPass
                ? outputFile
                : prefix + "lvl" + std::to_string(level) + "_" + std::to_string(index);

            {
                EdgeSink sink(outPath, 32u << 20);
                std::vector<FileSource> srcs;
                srcs.reserve(group);
                for (size_t j = i; j < hi; ++j)
                    srcs.emplace_back(runs[j], perSource, /*unlink=*/true);
                const uint64_t emitted = kwayMerge(srcs, sink, dedup);
                if (finalPass) finalRecords = emitted;
            }

            nextRuns.push_back(outPath);
            ++index;
        }

        std::printf("Passata di merge %zu: %zu run -> %zu run (%lld s).\n",
                    level, runs.size(), nextRuns.size(), seconds());
        std::fflush(stdout);

        runs.swap(nextRuns);
        ++level;
        if (finalPass) break;
    }

    // Se e' rimasta una sola run che non e' l'output finale (caso limite,
    // ad esempio con fanIn=1), la si copia semplicemente come output.
    if (runs.size() == 1 && runs[0] != outputFile) {
        EdgeSink sink(outputFile, 32u << 20);
        std::vector<FileSource> srcs;
        srcs.emplace_back(runs[0], 32u << 20, true);
        finalRecords = kwayMerge(srcs, sink, dedup);
    }

    std::printf("Fatto: %" PRIu64 " archi in output (da %" PRIu64 ") in %lld secondi.\n",
                finalRecords, totalRecords, seconds());
    return 0;
}
