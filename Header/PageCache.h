#pragma once
#include "Common.h"
#include <map>
#include <mutex>

namespace gzq_memoryPool
{

    class PageCache
    {
    public:
        static const size_t PAGE_SIZE = 4096; // 4Kҳ��С

        static PageCache& getInstance()
        {
            static PageCache instance;
            return instance;
        }

        // ����ָ��ҳ����span
        void* allocateSpan(size_t numPages);

        // �ͷ�span
        void deallocateSpan(void* ptr, size_t numPages);

    private:
        PageCache() = default;

        // ��ϵͳ�����ڴ�
        void* systemAlloc(size_t numPages);
    private:
        struct Span
        {
            void* pageAddr; // ҳ��ʼ��ַ
            size_t numPages; // ҳ��
            Span* next;     // ����ָ��
        };

        // ��ҳ���������span����ͬҳ����Ӧ��ͬSpan����
        std::map<size_t, Span*> freeSpans_;
        // ҳ�ŵ�span��ӳ�䣬���ڻ���
        std::map<void*, Span*> spanMap_;
        std::mutex mutex_;
    };

} // namespace memoryPool