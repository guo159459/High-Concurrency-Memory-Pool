#include "CentralCache.h"
#include "PageCache.h"
#include <cassert>
#include <thread>
#include <chrono>

namespace Kama_memoryPool
{

    const std::chrono::milliseconds CentralCache::DELAY_INTERVAL{ 1000 };

    // ÿ�δ�PageCache��ȡspan��С����ҳΪ��λ��
    static const size_t SPAN_PAGES = 8;

    CentralCache::CentralCache()
    {
        for (auto& ptr : centralFreeList_)
        {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        for (auto& lock : locks_)
        {
            lock.clear();
        }
        // ��ʼ���ӳٹ黹��صĳ�Ա����
        for (auto& count : delayCounts_)
        {
            count.store(0, std::memory_order_relaxed);
        }
        for (auto& time : lastReturnTimes_)
        {
            time = std::chrono::steady_clock::now();
        }
        spanCount_.store(0, std::memory_order_relaxed);
    }

    void* CentralCache::fetchRange(size_t index)
    {
        // ������飬���������ڵ���FREE_LIST_SIZEʱ��˵�������ڴ����Ӧֱ����ϵͳ����
        if (index >= FREE_LIST_SIZE)
            return nullptr;

        // ����������
        while (locks_[index].test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield(); // ����߳��ò�������æ�ȴ��������������CPU
        }

        void* result = nullptr;
        try
        {
            // ���Դ����Ļ����ȡ�ڴ��
            result = centralFreeList_[index].load(std::memory_order_relaxed);

            if (!result)
            {
                // ������Ļ���Ϊ�գ���ҳ�����ȡ�µ��ڴ��
                size_t size = (index + 1) * ALIGNMENT;
                result = fetchFromPageCache(size);

                if (!result)
                {
                    locks_[index].clear(std::memory_order_release);
                    return nullptr;
                }

                // ����ȡ���ڴ���зֳ�С��
                char* start = static_cast<char*>(result);

                // ����ʵ�ʷ����ҳ��
                size_t numPages = (size <= SPAN_PAGES * PageCache::PAGE_SIZE) ?
                    SPAN_PAGES : (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;
                // ʹ��ʵ��ҳ���������
                size_t blockNum = (numPages * PageCache::PAGE_SIZE) / size;

                if (blockNum > 1)
                {  // ȷ��������������Ź�������
                    for (size_t i = 1; i < blockNum; ++i)
                    {
                        void* current = start + (i - 1) * size;
                        void* next = start + i * size;
                        *reinterpret_cast<void**>(current) = next;
                    }
                    *reinterpret_cast<void**>(start + (blockNum - 1) * size) = nullptr;

                    // ����result����һ���ڵ�
                    void* next = *reinterpret_cast<void**>(result);
                    // ��result������Ͽ�
                    *reinterpret_cast<void**>(result) = nullptr;
                    // �������Ļ���
                    centralFreeList_[index].store(
                        next,
                        std::memory_order_release
                    );

                    // ʹ��������ʽ��¼span��Ϣ
                    size_t trackerIndex = spanCount_++;
                    if (trackerIndex < spanTrackers_.size())
                    {
                        spanTrackers_[trackerIndex].spanAddr.store(start, std::memory_order_release);
                        spanTrackers_[trackerIndex].numPages.store(numPages, std::memory_order_release);
                        spanTrackers_[trackerIndex].blockCount.store(blockNum, std::memory_order_release); // ��������blockNum���ڴ��
                        spanTrackers_[trackerIndex].freeCount.store(blockNum - 1, std::memory_order_release); // ��һ����result�ѱ������ȥ�����Գ�ʼ���п���ΪblockNum - 1
                    }
                }
            }
            else
            {
                // ����result����һ���ڵ�
                void* next = *reinterpret_cast<void**>(result);
                // ��result������Ͽ�
                *reinterpret_cast<void**>(result) = nullptr;

                // �������Ļ���
                centralFreeList_[index].store(next, std::memory_order_release);

                // ����span�Ŀ��м���
                SpanTracker* tracker = getSpanTracker(result);
                if (tracker)
                {
                    // ����һ�����п�
                    tracker->freeCount.fetch_sub(1, std::memory_order_release);
                }
            }
        }
        catch (...)
        {
            locks_[index].clear(std::memory_order_release);
            throw;
        }

        // �ͷ���
        locks_[index].clear(std::memory_order_release);
        return result;
    }

    void CentralCache::returnRange(void* start, size_t size, size_t index)
    {
        if (!start || index >= FREE_LIST_SIZE)
            return;

        size_t blockSize = (index + 1) * ALIGNMENT;
        size_t blockCount = size / blockSize;

        while (locks_[index].test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        try
        {
            // 1. ���黹���������ӵ����Ļ���
            void* end = start;
            size_t count = 1;
            while (*reinterpret_cast<void**>(end) != nullptr && count < blockCount) {
                end = *reinterpret_cast<void**>(end);
                count++;
            }
            void* current = centralFreeList_[index].load(std::memory_order_relaxed);
            *reinterpret_cast<void**>(end) = current; // ͷ�巨����ԭ��������ڹ黹�����ߣ�
            centralFreeList_[index].store(start, std::memory_order_release);

            // 2. �����ӳټ���
            size_t currentCount = delayCounts_[index].fetch_add(1, std::memory_order_relaxed) + 1;
            auto currentTime = std::chrono::steady_clock::now();

            // 3. ����Ƿ���Ҫִ���ӳٹ黹
            if (shouldPerformDelayedReturn(index, currentCount, currentTime))
            {
                performDelayedReturn(index);
            }
        }
        catch (...)
        {
            locks_[index].clear(std::memory_order_release);
            throw;
        }

        locks_[index].clear(std::memory_order_release);
    }

    // ����Ƿ���Ҫִ���ӳٹ黹
    bool CentralCache::shouldPerformDelayedReturn(size_t index, size_t currentCount,
        std::chrono::steady_clock::time_point currentTime)
    {
        // ���ڼ�����ʱ���˫�ؼ��
        if (currentCount >= MAX_DELAY_COUNT)
        {
            return true;
        }

        auto lastTime = lastReturnTimes_[index];
        return (currentTime - lastTime) >= DELAY_INTERVAL;
    }

    // ִ���ӳٹ黹
    void CentralCache::performDelayedReturn(size_t index)
    {
        // �����ӳټ���
        delayCounts_[index].store(0, std::memory_order_relaxed);
        // �������黹ʱ��
        lastReturnTimes_[index] = std::chrono::steady_clock::now();

        // ͳ��ÿ��span�Ŀ��п���
        std::unordered_map<SpanTracker*, size_t> spanFreeCounts;
        void* currentBlock = centralFreeList_[index].load(std::memory_order_relaxed);

        while (currentBlock)
        {
            SpanTracker* tracker = getSpanTracker(currentBlock);
            if (tracker)
            {
                spanFreeCounts[tracker]++;
            }
            currentBlock = *reinterpret_cast<void**>(currentBlock);
        }

        // ����ÿ��span�Ŀ��м���������Ƿ���Թ黹
        for (const auto& [tracker, newFreeBlocks] : spanFreeCounts)
        {
            updateSpanFreeCount(tracker, newFreeBlocks, index);
        }
    }

    void CentralCache::updateSpanFreeCount(SpanTracker* tracker, size_t newFreeBlocks, size_t index)
    {
        size_t oldFreeCount = tracker->freeCount.load(std::memory_order_relaxed);
        size_t newFreeCount = oldFreeCount + newFreeBlocks;
        tracker->freeCount.store(newFreeCount, std::memory_order_release);

        // ������п鶼���У��黹span
        if (newFreeCount == tracker->blockCount.load(std::memory_order_relaxed))
        {
            void* spanAddr = tracker->spanAddr.load(std::memory_order_relaxed);
            size_t numPages = tracker->numPages.load(std::memory_order_relaxed);

            // �������������Ƴ���Щ��
            void* head = centralFreeList_[index].load(std::memory_order_relaxed);
            void* newHead = nullptr;
            void* prev = nullptr;
            void* current = head;

            while (current)
            {
                void* next = *reinterpret_cast<void**>(current);
                if (current >= spanAddr &&
                    current < static_cast<char*>(spanAddr) + numPages * PageCache::PAGE_SIZE)
                {
                    if (prev)
                    {
                        *reinterpret_cast<void**>(prev) = next;
                    }
                    else
                    {
                        newHead = next;
                    }
                }
                else
                {
                    prev = current;
                }
                current = next;
            }

            centralFreeList_[index].store(newHead, std::memory_order_release);
            PageCache::getInstance().deallocateSpan(spanAddr, numPages);
        }
    }

    void* CentralCache::fetchFromPageCache(size_t size)
    {
        // 1. ����ʵ����Ҫ��ҳ��
        size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

        // 2. ���ݴ�С�����������
        if (size <= SPAN_PAGES * PageCache::PAGE_SIZE)
        {
            // С�ڵ���32KB������ʹ�ù̶�8ҳ
            return PageCache::getInstance().allocateSpan(SPAN_PAGES);
        }
        else
        {
            // ����32KB�����󣬰�ʵ���������
            return PageCache::getInstance().allocateSpan(numPages);
        }
    }

    SpanTracker* CentralCache::getSpanTracker(void* blockAddr)
    {
        // ����spanTrackers_���飬�ҵ�blockAddr������span
        for (size_t i = 0; i < spanCount_.load(std::memory_order_relaxed); ++i)
        {
            void* spanAddr = spanTrackers_[i].spanAddr.load(std::memory_order_relaxed);
            size_t numPages = spanTrackers_[i].numPages.load(std::memory_order_relaxed);

            if (blockAddr >= spanAddr &&
                blockAddr < static_cast<char*>(spanAddr) + numPages * PageCache::PAGE_SIZE)
            {
                return &spanTrackers_[i];
            }
        }
        return nullptr;
    }

} // namespace memoryPool