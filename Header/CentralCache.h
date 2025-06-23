#pragma once
#include "Common.h"
#include <mutex>
#include <unordered_map>
#include <array>
#include <atomic>
#include <chrono>

namespace gzq_memoryPool
{

    // ʹ��������span��Ϣ�洢
    struct SpanTracker {
        std::atomic<void*> spanAddr{ nullptr };
        std::atomic<size_t> numPages{ 0 };
        std::atomic<size_t> blockCount{ 0 };
        std::atomic<size_t> freeCount{ 0 }; // ����׷��spn�л��ж��ٿ��ǿ��еģ�������п鶼���У���黹span��PageCache
    };

    class CentralCache
    {
    public:
        static CentralCache& getInstance()
        {
            static CentralCache instance;
            return instance;
        }

        void* fetchRange(size_t index);
        void returnRange(void* start, size_t size, size_t index);

    private:
        // �໥�ǻ�����ԭ��ָ��Ϊnullptr
        CentralCache();
        // ��ҳ�����ȡ�ڴ�
        void* fetchFromPageCache(size_t size);

        // ��ȡspan��Ϣ
        SpanTracker* getSpanTracker(void* blockAddr);

        // ����span�Ŀ��м���������Ƿ���Թ黹
        void updateSpanFreeCount(SpanTracker* tracker, size_t newFreeBlocks, size_t index);

    private:
        // ���Ļ������������
        std::array<std::atomic<void*>, FREE_LIST_SIZE> centralFreeList_;

        // ����ͬ����������
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;

        // ʹ������洢span��Ϣ������map�Ŀ���
        std::array<SpanTracker, 1024> spanTrackers_;
        std::atomic<size_t> spanCount_{ 0 };

        // �ӳٹ黹��صĳ�Ա����
        static const size_t MAX_DELAY_COUNT = 48;  // ����ӳټ���
        std::array<std::atomic<size_t>, FREE_LIST_SIZE> delayCounts_;  // ÿ����С����ӳټ���
        std::array<std::chrono::steady_clock::time_point, FREE_LIST_SIZE> lastReturnTimes_;  // �ϴι黹ʱ��
        static const std::chrono::milliseconds DELAY_INTERVAL;  // �ӳټ��

        bool shouldPerformDelayedReturn(size_t index, size_t currentCount, std::chrono::steady_clock::time_point currentTime);
        void performDelayedReturn(size_t index);
    };

} // namespace memoryPool