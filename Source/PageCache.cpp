#include "PageCache.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <cstring>

namespace gzq_memoryPool
{

void* PageCache::allocateSpan(size_t numPages)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // ���Һ��ʵĿ���span
    // lower_bound�������ص�һ�����ڵ���numPages��Ԫ�صĵ�����
    auto it = freeSpans_.lower_bound(numPages);
    if (it != freeSpans_.end())
    {
        Span* span = it->second;

        // ��ȡ����span��ԭ�еĿ�������freeSpans_[it->first]���Ƴ�
        if (span->next)
        {
            freeSpans_[it->first] = span->next;
        }
        else
        {
            freeSpans_.erase(it);
        }

        // ���span������Ҫ��numPages����зָ�
        if (span->numPages > numPages) 
        {
            Span* newSpan = new Span;
            newSpan->pageAddr = static_cast<char*>(span->pageAddr) + 
                                numPages * PAGE_SIZE;
            newSpan->numPages = span->numPages - numPages;
            newSpan->next = nullptr;

            // ���������ַŻؿ���Span*�б�ͷ��
            auto& list = freeSpans_[newSpan->numPages];
            newSpan->next = list;
            list = newSpan;

            span->numPages = numPages;
        }

        // ��¼span��Ϣ���ڻ���
        spanMap_[span->pageAddr] = span;
        return span->pageAddr;
    }

    // û�к��ʵ�span����ϵͳ����
    void* memory = systemAlloc(numPages);
    if (!memory) return nullptr;

    // �����µ�span
    Span* span = new Span;
    span->pageAddr = memory;
    span->numPages = numPages;
    span->next = nullptr;

    // ��¼span��Ϣ���ڻ���
    spanMap_[memory] = span;
    return memory;
}

void PageCache::deallocateSpan(void* ptr, size_t numPages)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // ���Ҷ�Ӧ��span��û�ҵ�������PageCache������ڴ棬ֱ�ӷ���
    auto it = spanMap_.find(ptr);
    if (it == spanMap_.end()) return;

    Span* span = it->second;

    // ���Ժϲ����ڵ�span
    void* nextAddr = static_cast<char*>(ptr) + numPages * PAGE_SIZE;
    auto nextIt = spanMap_.find(nextAddr);
    
    if (nextIt != spanMap_.end())
    {
        Span* nextSpan = nextIt->second;
        
        // 1. ���ȼ��nextSpan�Ƿ��ڿ���������
        bool found = false;
        auto& nextList = freeSpans_[nextSpan->numPages];
        
        // ����Ƿ���ͷ�ڵ�
        if (nextList == nextSpan)
        {
            nextList = nextSpan->next;
            found = true;
        }
        else if (nextList) // ֻ��������ǿ�ʱ�ű���
        {
            Span* prev = nextList;
            while (prev->next)
            {
                if (prev->next == nextSpan)
                {   
                    // ��nextSpan�ӿ����������Ƴ�
                    prev->next = nextSpan->next;
                    found = true;
                    break;
                }
                prev = prev->next;
            }
        }

        // 2. ֻ�����ҵ�nextSpan������²Ž��кϲ�
        if (found)
        {
            // �ϲ�span
            span->numPages += nextSpan->numPages;
            spanMap_.erase(nextAddr);
            delete nextSpan;
        }
    }

    // ���ϲ����spanͨ��ͷ�巨��������б�
    auto& list = freeSpans_[span->numPages];
    span->next = list;
    list = span;
}

void* PageCache::systemAlloc(size_t numPages)
{
    size_t size = numPages * PAGE_SIZE;
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ptr == nullptr) return nullptr;
    // ʹ��mmap�����ڴ�
    /*void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;*/

    // �����ڴ�
    memset(ptr, 0, size);
    return ptr;
}

} // namespace memoryPool