#ifndef TLSF_ALLOCATOR_HPP
#define TLSF_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <array>

class TLSFAllocator {
public:
    explicit TLSFAllocator(std::size_t memoryPoolSize);
    ~TLSFAllocator();

    void* allocate(std::size_t size);
    void deallocate(void* ptr);

    void* getMemoryPoolStart() const { return memoryPool; }
    std::size_t getMemoryPoolSize() const { return poolSize; }
    std::size_t getMaxAvailableBlockSize() const;

    TLSFAllocator(const TLSFAllocator&) = delete;
    TLSFAllocator& operator=(const TLSFAllocator&) = delete;
    TLSFAllocator(TLSFAllocator&&) = delete;
    TLSFAllocator& operator=(TLSFAllocator&&) = delete;

private:
    void* memoryPool;
    std::size_t poolSize;

    static constexpr int FLI_BITS = 5;
    static constexpr int SLI_BITS = 4;
    static constexpr int FLI_SIZE = (1 << FLI_BITS);
    static constexpr int SLI_SIZE = (1 << SLI_BITS);

    struct BlockHeader {
        std::size_t size;     // including header
        bool isFree;
        BlockHeader* prevPhysBlock;
        BlockHeader* nextPhysBlock;
    };

    struct FreeBlock : BlockHeader {
        FreeBlock* prevFree;
        FreeBlock* nextFree;
    };

    struct TLSFIndex {
        std::array<std::array<FreeBlock*, SLI_SIZE>, FLI_SIZE> freeLists{};
        std::uint32_t fliBitmap = 0;
        std::array<std::uint16_t, FLI_SIZE> sliBitmaps{};
    };

    TLSFIndex index;

    void initializeMemoryPool(std::size_t size);
    void splitBlock(FreeBlock* block, std::size_t reqSize);
    void mergeAdjacentFreeBlocks(FreeBlock* block);
    FreeBlock* findSuitableBlock(std::size_t size);
    void insertFreeBlock(FreeBlock* block);
    void removeFreeBlock(FreeBlock* block);
    void mappingFunction(std::size_t size, int& fli, int& sli);
};

#endif // TLSF_ALLOCATOR_HPP
