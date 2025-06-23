#pragma once
#include "Common.h"

namespace gzq_memoryPool
{

    // �̱߳��ػ���
    class ThreadCache
    {
    public://����ģʽ
        static ThreadCache* getInstance()
        {
            static thread_local ThreadCache instance;//�ֲ��߳�
            return &instance;
        }

        void* allocate(size_t size);
        void deallocate(void* ptr, size_t size);
    private:
        ThreadCache()
        {
            // ��ʼ����������ʹ�Сͳ��
            freeList_.fill(nullptr);
            freeListSize_.fill(0);
        }

        // �����Ļ����ȡ�ڴ�
        void* fetchFromCentralCache(size_t index);
        // �黹�ڴ浽���Ļ���
        void returnToCentralCache(void* start, size_t size);

        bool shouldReturnToCentralCache(size_t index);
    private:
        // ÿ���̵߳�������������
        std::array<void*, FREE_LIST_SIZE>  freeList_;
        std::array<size_t, FREE_LIST_SIZE> freeListSize_; // ���������Сͳ��   
    };

} // namespace memoryPool