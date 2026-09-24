// pg_el_builder.cpp
//
// Costruisce la raw edge list del Bitcoin Payment Graph in formato binario,
// leggendo una o piu' liste di transazioni in sequenza.
//
// Formato di output: sequenza di coppie di interi a 64 bit senza segno, in
// byte order nativo della macchina che esegue il programma (nessuna
// conversione), senza header e senza padding. Ogni arco occupa esattamente
// 16 byte: [sourceNodeId (8 byte)][targetNodeId (8 byte)].
//
// ATTENZIONE: il file prodotto e' leggibile correttamente solo su macchine
// con lo stesso byte order di quella che lo ha generato (little-endian sulla
// stragrande maggioranza delle CPU x86/ARM odierne). Se il file deve essere
// scambiato fra macchine con byte order diverso, va reintrodotta una
// conversione esplicita (ad esempio a big-endian, come nella versione
// precedente di questo programma).
//
// Compilazione:
//   g++ -O3 -march=native -std=c++17 -o pg_el_builder pg_el_builder.cpp
//
// Uso:
//   ./pg_el_builder <inputFile1> [inputFile2 ...] <edgeListFile>
//
// I file di input vengono letti uno dopo l'altro, nell'ordine in cui sono
// passati sulla riga di comando, come se fossero un unico file concatenato:
// non c'e' nessuna scrittura intermedia su disco. Questo presuppone che il
// txId di ogni riga sia univoco sull'intero insieme di file, cioe' che gli
// input possano riferirsi anche a transazioni lette da un file precedente.
// Se invece ogni file ha una numerazione dei txId indipendente dagli altri,
// questo programma non e' adatto cosi' com'e'.
//
// Se una transazione con lo stesso txId compare piu' volte (nello stesso
// file o in file diversi), viene considerata solo la prima occorrenza letta:
// tutte le successive vengono scartate per intero (senza assegnare nuovi id
// ai loro output e senza generare archi dai loro input).

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

// ---------------------------------------------------------------------------
// Parametri
// ---------------------------------------------------------------------------

static constexpr size_t READ_BUFFER_SIZE  = 64u << 20;   // 64 MiB
static constexpr size_t WRITE_BUFFER_SIZE = 32u << 20;   // 32 MiB
static constexpr size_t MAX_LINE_SIZE     = 16u << 20;

// ---------------------------------------------------------------------------
// Lettore sequenziale a blocchi, su piu' file letti uno dopo l'altro.
//
// I file vengono attraversati nell'ordine in cui sono passati al costruttore
// ed esposti a chi chiama next() come un unico stream ininterrotto: quando
// un file finisce si passa al successivo senza che il chiamante se ne
// accorga. Questo equivale a concatenare i file prima di leggerli, ma senza
// scrivere nulla su disco (si veda la nota nel main sull'ipotesi che questo
// comporta sulla numerazione dei txId).
//
// L'ultima riga di un file viene sempre trattata come completa anche se non
// termina con '\n', cosi' che non si fonda mai con la prima riga del file
// successivo.
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
                    // Ultima riga del file corrente, priva di terminatore:
                    // va restituita per intero prima di passare al file
                    // successivo, senza fonderla con quel che verra' dopo.
                    begin = buf_.data() + pos_;
                    end   = buf_.data() + len_;
                    if (end > begin && end[-1] == '\r') --end;
                    pos_ = len_;
                    return true;
                }
                if (!openNext()) return false;   // nessun altro file da leggere
                continue;
            }
            refill();
        }
    }

    // Nome del file attualmente in lettura (utile per i log di avanzamento).
    const std::string& currentPath() const { return paths_[fileIndex_ - 1]; }

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
        std::printf("Lettura di %s...\n", path.c_str());
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
                std::fprintf(stderr, "Riga troppo lunga (> %zu byte).\n", buf_.size());
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
// Scrittore binario bufferizzato
// ---------------------------------------------------------------------------

class EdgeWriter {
public:
    explicit EdgeWriter(const char* path) {
        fd_ = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) { perror("open(output)"); std::exit(1); }
        buf_.resize(WRITE_BUFFER_SIZE);
    }

    ~EdgeWriter() { flush(); if (fd_ >= 0) ::close(fd_); }

    inline void write(uint64_t source, uint64_t target) {
        if (__builtin_expect(pos_ + 16 > buf_.size(), 0)) flush();
        std::memcpy(buf_.data() + pos_,     &source, 8);
        std::memcpy(buf_.data() + pos_ + 8, &target, 8);
        pos_ += 16;
    }

    void flush() {
        size_t off = 0;
        while (off < pos_) {
            ssize_t n = ::write(fd_, buf_.data() + off, pos_ - off);
            if (n <= 0) { perror("write(output)"); std::exit(1); }
            off += static_cast<size_t>(n);
        }
        pos_ = 0;
    }

private:
    int fd_ = -1;
    std::vector<char> buf_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Parsing
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

static inline const char* skipFields(const char* p, const char* e, int n, char sep) {
    while (n-- > 0) {
        const char* s = static_cast<const char*>(::memchr(p, sep, static_cast<size_t>(e - p)));
        if (!s) return e;
        p = s + 1;
    }
    return p;
}

// Numero di campi separati da `sep`, con la semantica di String.split() in
// Java (i campi vuoti finali vengono scartati).
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
        std::fprintf(stderr, "Usage: %s <inputFile1> [inputFile2 ...] <edgeListFile>\n", argv[0]);
        return 1;
    }
    // L'ultimo argomento e' il file di output, tutti i precedenti sono i
    // file di input, nell'ordine in cui sono stati specificati.
    const char* edgeListFile = argv[argc - 1];
    std::vector<std::string> inputFiles;
    inputFiles.reserve(static_cast<size_t>(argc - 2));
    for (int i = 1; i < argc - 1; ++i) inputFiles.emplace_back(argv[i]);

    LineReader in(std::move(inputFiles));
    EdgeWriter out(edgeListFile);

    // Gli id dei nodi sono assegnati in modo strettamente sequenziale e gli
    // output di una transazione vengono numerati consecutivamente: l'id del
    // nodo (txId, offset) e' quindi sempre base[txId] + offset. Non serve
    // nessuna hash map.
    std::vector<uint64_t> base;
    std::vector<uint32_t> cnt;
    std::vector<bool> seen;   // seen[txId] = true se il txId e' gia' stato processato
    base.reserve(1u << 20);
    cnt.reserve(1u << 20);
    seen.reserve(1u << 20);

    std::vector<uint64_t> sources;   // buffer riusato: id sorgente della tx corrente

    uint64_t nextId = 0;
    uint64_t txCount = 0, nodeCount = 0, edgeCount = 0;
    uint64_t missingInputs = 0;
    uint64_t duplicateCount = 0;

    auto startTime = std::chrono::steady_clock::now();
    auto seconds = [&]() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - startTime).count();
    };

    const char* lb;
    const char* le;
    while (in.next(lb, le)) {
        if (lb == le) continue;

        const char* c1 = static_cast<const char*>(::memchr(lb, ':', static_cast<size_t>(le - lb)));
        if (!c1) continue;
        const char* c2 = static_cast<const char*>(::memchr(c1 + 1, ':', static_cast<size_t>(le - c1 - 1)));
        if (!c2) continue;

        // Identificativo della transazione corrente (terzo campo di info).
        const char* q = skipFields(lb, c1, 2, ',');
        const int64_t txId = parseInt(q, c1);
        if (txId < 0) continue;

        // Transazione duplicata (stesso txId gia' incontrato in precedenza,
        // in questo file o in uno dei precedenti): si tiene solo la prima
        // occorrenza, questa e ogni successiva vengono ignorate per intero.
        if (static_cast<size_t>(txId) < seen.size() && seen[static_cast<size_t>(txId)]) {
            ++duplicateCount;
            continue;
        }

        const uint32_t outCount = countFields(c2 + 1, le, ';');

        if (static_cast<size_t>(txId) >= base.size()) {
            size_t newSize = static_cast<size_t>(txId) + 1;
            if (newSize < base.size() * 2) newSize = base.size() * 2;
            base.resize(newSize, 0);
            cnt.resize(newSize, 0);
            seen.resize(newSize, false);
        }
        seen[static_cast<size_t>(txId)] = true;
        const uint64_t myBase = nextId;
        base[static_cast<size_t>(txId)] = myBase;
        cnt[static_cast<size_t>(txId)]  = outCount;
        nextId    += outCount;
        nodeCount += outCount;

        if (outCount > 0 && c1 + 1 < c2) {
            // Raccoglie gli id sorgente della transazione corrente.
            sources.clear();
            const char* p = c1 + 1;
            while (p < c2) {
                const char* fe = static_cast<const char*>(::memchr(p, ';', static_cast<size_t>(c2 - p)));
                if (!fe) fe = c2;
                if (fe > p) {
                    const char* r = skipFields(p, fe, 2, ',');
                    const int64_t prevTxId = parseInt(r, fe);
                    if (r < fe && *r == ',') ++r;
                    const int64_t prevOffset = parseInt(r, fe);

                    if (prevTxId >= 0 && static_cast<size_t>(prevTxId) < base.size() &&
                        prevOffset >= 0 &&
                        static_cast<uint32_t>(prevOffset) < cnt[static_cast<size_t>(prevTxId)]) {
                        sources.push_back(base[static_cast<size_t>(prevTxId)] +
                                          static_cast<uint64_t>(prevOffset));
                    } else {
                        ++missingInputs;
                    }
                }
                p = fe + 1;
            }

            // Due archi identici possono nascere solo da due input identici
            // della stessa transazione: deduplicando qui, la edge list globale
            // risulta gia' priva di duplicati.
            if (sources.size() > 1) {
                std::sort(sources.begin(), sources.end());
                sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
            }

            for (uint64_t src : sources) {
                for (uint32_t i = 0; i < outCount; ++i) out.write(src, myBase + i);
                edgeCount += outCount;
            }
        }

        ++txCount;
        if (txCount % 10000000 == 0) {
            std::printf("Processed: %" PRIu64 " transactions (after %lld seconds).\n",
                        txCount, static_cast<long long>(seconds()));
            std::fflush(stdout);
        }
    }

    out.flush();

    std::printf("Processed: %" PRIu64 " transactions (after %lld seconds).\n",
                txCount, static_cast<long long>(seconds()));
    std::printf("Nodes: %" PRIu64 "\tEdges: %" PRIu64 "\n", nodeCount, edgeCount);
    if (duplicateCount)
        std::printf("Transazioni duplicate ignorate: %" PRIu64 "\n", duplicateCount);
    if (missingInputs)
        std::fprintf(stderr, "Warning: %" PRIu64 " input non risolti.\n", missingInputs);
    return 0;
}
