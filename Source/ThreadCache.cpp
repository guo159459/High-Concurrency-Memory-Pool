#include "ThreadCache.h"
#include "CentralCache.h"

namespace gzq_memoryPool
{

    void* ThreadCache::allocate(size_t size)//�ڴ�ķ���ʵ����Ҫ�ж�freeListSize_[index]�������ڴ�
    {
        // ����0��С�ķ�������
        if (size == 0)
        {
            size = ALIGNMENT; // ���ٷ���һ�������С
        }

        if (size > MAX_BYTES)
        {
            // �����ֱ�Ӵ�ϵͳ����
            return malloc(size);
        }

        size_t index = SizeClass::getIndex(size);//��ȡһ����size��Ӧ������ֵ

        // ���¶�Ӧ��������ĳ��ȼ���
        freeListSize_[index]--;

        // ����̱߳�����������
        // ��� freeList_[index] ��Ϊ�գ���ʾ���������п����ڴ��
        if (void* ptr = freeList_[index])
        {
            freeList_[index] = *reinterpret_cast<void**>(ptr); // ��freeList_[index]ָ����ڴ�����һ���ڴ���ַ��ȡ�����ڴ���ʵ�֣�
            return ptr;
        }

        // ����̱߳�����������Ϊ�գ�������Ļ����ȡһ���ڴ�
        return fetchFromCentralCache(index);
    }

    void ThreadCache::deallocate(void* ptr, size_t size)//�ڴ��ͷ�
    {
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }

        size_t index = SizeClass::getIndex(size);

        // ���뵽�̱߳�����������
        *reinterpret_cast<void**>(ptr) = freeList_[index];
        freeList_[index] = ptr;

        // ���¶�Ӧ��������ĳ��ȼ���
        freeListSize_[index]++;

        // �ж��Ƿ���Ҫ�������ڴ���ո����Ļ���
        if (shouldReturnToCentralCache(index))
        {
            returnToCentralCache(freeList_[index], size);
        }
    }

    // �ж��Ƿ���Ҫ���ڴ���ո����Ļ���
    bool ThreadCache::shouldReturnToCentralCache(size_t index)
    {
        // �趨��ֵ�����磺����������Ĵ�С����һ������ʱ
        size_t threshold = 256;
        return (freeListSize_[index] > threshold);
    }

    void* ThreadCache::fetchFromCentralCache(size_t index)
    {
        // �����Ļ���������ȡ�ڴ�
        void* start = CentralCache::getInstance().fetchRange(index);
        if (!start) return nullptr;

        // ȡһ�����أ����������������
        void* result = start;
        freeList_[index] = *reinterpret_cast<void**>(start);

        // �������������С
        size_t batchNum = 0;
        void* current = start; // ��start��ʼ����

        // ��������Ļ����ȡ���ڴ������
        while (current != nullptr)
        {
            batchNum++;
            current = *reinterpret_cast<void**>(current); // ������һ���ڴ��
        }

        // ����freeListSize_�����ӻ�ȡ���ڴ������
        freeListSize_[index] += batchNum;

        return result;
    }

    void ThreadCache::returnToCentralCache(void* start, size_t size)
    {
        // ���ݴ�С�����Ӧ������
        size_t index = SizeClass::getIndex(size);

        // ��ȡ������ʵ�ʿ��С
        size_t alignedSize = SizeClass::roundUp(size);

        // ����Ҫ�黹�ڴ������
        size_t batchNum = freeListSize_[index];
        if (batchNum <= 1) return; // ���ֻ��һ���飬�򲻹黹

        // ����һ������ThreadCache�У����籣��1/4��
        size_t keepNum = std::max(batchNum / 4, size_t(1));
        size_t returnNum = batchNum - keepNum;

        // ���ڴ�鴮������
        char* current = static_cast<char*>(start);
        // ʹ�ö����Ĵ�С����ָ��
        char* splitNode = current;
        for (size_t i = 0; i < keepNum - 1; ++i)
        {
            splitNode = reinterpret_cast<char*>(*reinterpret_cast<void**>(splitNode));
            if (splitNode == nullptr)
            {
                // ���������ǰ����������ʵ�ʵķ�������
                returnNum = batchNum - (i + 1);
                break;
            }
        }

        if (splitNode != nullptr)
        {
            // ��Ҫ���صĲ��ֺ�Ҫ�����Ĳ��ֶϿ�
            void* nextNode = *reinterpret_cast<void**>(splitNode);
            *reinterpret_cast<void**>(splitNode) = nullptr; // �Ͽ�����

            // ����ThreadCache�Ŀ�������
            freeList_[index] = start;

            // �������������С
            freeListSize_[index] = keepNum;

            // ��ʣ�ಿ�ַ��ظ�CentralCache
            if (returnNum > 0 && nextNode != nullptr)
            {
                CentralCache::getInstance().returnRange(nextNode, returnNum * alignedSize, index);
            }
        }
    }


} // namespace memoryPool