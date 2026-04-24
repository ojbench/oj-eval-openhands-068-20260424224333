#include "allocator.hpp"
#include <algorithm>
#include <cstring>
#include <new>

namespace {
constexpr std::size_t ALIGN = alignof(std::max_align_t);
inline std::size_t align_up(std::size_t x, std::size_t a = ALIGN) {
    return (x + (a - 1)) & ~(a - 1);
}
}

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) : memoryPool(nullptr), poolSize(align_up(memoryPoolSize)) {
    initializeMemoryPool(poolSize);
}

TLSFAllocator::~TLSFAllocator() {
    ::operator delete(memoryPool, std::align_val_t(ALIGN));
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    memoryPool = ::operator new(size, std::align_val_t(ALIGN));
    // Construct a single large free block spanning the pool
    auto* first = reinterpret_cast<FreeBlock*>(memoryPool);
    first->size = size;
    first->isFree = true;
    first->prevPhysBlock = nullptr;
    first->nextPhysBlock = nullptr;
    first->prevFree = nullptr;
    first->nextFree = nullptr;

    // reset index
    index.fliBitmap = 0;
    for (auto& s : index.sliBitmaps) s = 0;
    for (auto& arr : index.freeLists) arr.fill(nullptr);

    insertFreeBlock(first);
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    size = std::max<std::size_t>(size, sizeof(FreeBlock));
    int fl = 0;
    std::size_t tmp = size;
    while (tmp >>= 1) ++fl; // floor(log2(size))
    fli = std::min(fl, FLI_SIZE - 1);

    std::size_t base = std::size_t(1) << fli;
    int divisions = (int)std::min<std::size_t>(SLI_SIZE, base);
    std::size_t step = base / (divisions ? divisions : 1);
    std::size_t offset = (size > base) ? (size - base) : 0;
    int idx = step ? int(offset / step) : 0;
    if (idx >= divisions) idx = divisions - 1;
    sli = idx;
}

void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli; mappingFunction(block->size, fli, sli);
    block->isFree = true;
    block->prevFree = nullptr;
    block->nextFree = index.freeLists[fli][sli];
    if (block->nextFree) block->nextFree->prevFree = block;
    index.freeLists[fli][sli] = block;
    index.sliBitmaps[fli] |= (1u << sli);
    index.fliBitmap |= (1u << fli);
}

void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli; mappingFunction(block->size, fli, sli);
    FreeBlock* head = index.freeLists[fli][sli];
    if (head == block) {
        index.freeLists[fli][sli] = block->nextFree;
        if (block->nextFree) block->nextFree->prevFree = nullptr;
    } else {
        if (block->prevFree) block->prevFree->nextFree = block->nextFree;
        if (block->nextFree) block->nextFree->prevFree = block->prevFree;
    }
    if (!index.freeLists[fli][sli]) {
        index.sliBitmaps[fli] &= ~(1u << sli);
        if (index.sliBitmaps[fli] == 0) index.fliBitmap &= ~(1u << fli);
    }
    block->prevFree = block->nextFree = nullptr;
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli; mappingFunction(size, fli, sli);
    // Try current bucket and above
    for (int fi = fli; fi < FLI_SIZE; ++fi) {
        std::uint16_t slbm = index.sliBitmaps[fi];
        if (fi == fli) {
            // mask out lower slis
            std::uint16_t mask = 0xFFFFu << sli;
            slbm &= mask;
        }
        if (slbm) {
            int sidx = __builtin_ctz((unsigned)slbm); // first set bit
            FreeBlock* b = index.freeLists[fi][sidx];
            if (b) return b;
        }
    }
    return nullptr;
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t reqSize) {
    // reqSize already includes header and alignment
    std::size_t remaining = block->size - reqSize;
    if (remaining < sizeof(FreeBlock)) return; // not worth splitting

    // carve new block at the end for simplicity
    auto* newFree = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(block) + reqSize);
    newFree->size = remaining;
    newFree->isFree = true;
    newFree->prevPhysBlock = block;
    newFree->nextPhysBlock = block->nextPhysBlock;
    if (newFree->nextPhysBlock) newFree->nextPhysBlock->prevPhysBlock = newFree;

    block->size = reqSize;
    block->nextPhysBlock = newFree;

    insertFreeBlock(newFree);
}

void* TLSFAllocator::allocate(std::size_t size) {
    if (size == 0) return nullptr;
    std::size_t total = align_up(size) + align_up(sizeof(FreeBlock));
    FreeBlock* block = findSuitableBlock(total);
    if (!block) return nullptr;
    removeFreeBlock(block);
    splitBlock(block, total);
    block->isFree = false;
    return reinterpret_cast<void*>(reinterpret_cast<char*>(block) + align_up(sizeof(FreeBlock)));
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    // Merge forward neighbor if free
    if (block->nextPhysBlock) {
        auto* next = static_cast<FreeBlock*>(block->nextPhysBlock);
        if (next->isFree) {
            removeFreeBlock(next);
            block->size += next->size;
            block->nextPhysBlock = next->nextPhysBlock;
            if (block->nextPhysBlock) block->nextPhysBlock->prevPhysBlock = block;
        }
    }
    // Merge backward neighbor if free
    if (block->prevPhysBlock) {
        auto* prev = static_cast<FreeBlock*>(block->prevPhysBlock);
        if (prev->isFree) {
            removeFreeBlock(prev);
            prev->size += block->size;
            prev->nextPhysBlock = block->nextPhysBlock;
            if (prev->nextPhysBlock) prev->nextPhysBlock->prevPhysBlock = prev;
            block = prev;
        }
    }
    // Caller inserts the coalesced block
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    auto* block = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(ptr) - align_up(sizeof(FreeBlock)));
    block->isFree = true;
    // Coalesce before inserting
    mergeAdjacentFreeBlocks(block);
    // Determine the head block after potential backward merge
    if (block->prevPhysBlock) {
        auto* prev = static_cast<FreeBlock*>(block->prevPhysBlock);
        if (prev->isFree) block = prev; // after merge, prev is the head
    }
    insertFreeBlock(block);
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    std::size_t maxsz = 0;
    for (int fi = 0; fi < FLI_SIZE; ++fi) {
        if (!index.sliBitmaps[fi]) continue;
        for (int si = 0; si < SLI_SIZE; ++si) {
            auto* b = index.freeLists[fi][si];
            for (; b; b = b->nextFree) {
                if (b->isFree && b->size > maxsz) maxsz = b->size;
            }
        }
    }
    if (maxsz < align_up(sizeof(FreeBlock))) return 0;
    return maxsz - align_up(sizeof(FreeBlock));
}
