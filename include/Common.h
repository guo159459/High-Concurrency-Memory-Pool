#pragma once
#include <cstddef>
#include <atomic>
#include <array>

namespace Kama_memoryPool
{
    // �������ʹ�С����
    constexpr size_t ALIGNMENT = 8;
    constexpr size_t MAX_BYTES = 256 * 1024; // 256KB
    constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // ALIGNMENT����ָ��void*�Ĵ�С

    // �ڴ��ͷ����Ϣ
    struct BlockHeader
    {
        size_t size; // �ڴ���С
        bool   inUse; // ʹ�ñ�־
        BlockHeader* next; // ָ����һ���ڴ��
    };

    // ��С�����
    class SizeClass
    {
    public:
        static size_t roundUp(size_t bytes)
        {
            return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        }

        static size_t getIndex(size_t bytes)
        {
            // ȷ��bytes����ΪALIGNMENT
            bytes = std::max(bytes, ALIGNMENT);
            // ����ȡ����-1
            return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
        }
    };

} // namespace memoryPool