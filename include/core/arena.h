#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

// per-thread scratch memory for the traversals, used strictly as a stack (released in LIFO order),
// so the hot loop never touches the heap allocator
class Arena {
public:
    struct Mark {
        size_t chunk;
        size_t used;
    };

    Arena() { chunks_.push_back(make_chunk(CHUNK_FLOATS)); }

    Mark mark() const { return {chunk_, used_}; }

    void release(Mark m) {
        chunk_ = m.chunk;
        used_ = m.used;
    }

    float* alloc(size_t n) {
        n = (n + 15) & ~size_t(15); // keeps every block 64 byte aligned
        if (used_ + n > chunks_[chunk_].size) {
            chunk_++;
            used_ = 0;
            if (chunk_ == chunks_.size()) chunks_.push_back(make_chunk(std::max(n, CHUNK_FLOATS)));
            else if (chunks_[chunk_].size < n) chunks_[chunk_] = make_chunk(std::max(n, CHUNK_FLOATS));
        }
        float* p = chunks_[chunk_].data.get() + used_;
        used_ += n;
        return p;
    }

private:
    static constexpr size_t CHUNK_FLOATS = size_t(1) << 20;

    struct Deleter {
        void operator()(float* p) const { ::operator delete[](p, std::align_val_t(64)); }
    };

    struct Chunk {
        std::unique_ptr<float[], Deleter> data;
        size_t size;
    };

    static Chunk make_chunk(size_t n) {
        float* p = static_cast<float*>(::operator new[](n * sizeof(float), std::align_val_t(64)));
        return {std::unique_ptr<float[], Deleter>(p), n};
    }

    std::vector<Chunk> chunks_;
    size_t chunk_ = 0;
    size_t used_ = 0;
};

// one arena per thread, padded so threads don't false-share the bookkeeping
struct alignas(64) PaddedArena {
    Arena arena;
};

// frees everything allocated through it when the scope ends
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) : arena_(arena), mark_(arena.mark()) {}
    ~ArenaScope() { arena_.release(mark_); }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

    float* alloc(size_t n) { return arena_.alloc(n); }

private:
    Arena& arena_;
    Arena::Mark mark_;
};
