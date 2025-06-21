#include "MemoryPool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <thread>
#include <array>

using namespace Kama_memoryPool;
using namespace std::chrono;

// ��ʱ����
class Timer
{
    high_resolution_clock::time_point start;
public:
    Timer() : start(high_resolution_clock::now()) {}

    double elapsed()
    {
        auto end = high_resolution_clock::now();
        return duration_cast<microseconds>(end - start).count() / 1000.0; // ת��Ϊ����
    }
};

// ���ܲ�����
class PerformanceTest
{
private:
    // ����ͳ����Ϣ
    struct TestStats
    {
        double memPoolTime{ 0.0 };
        double systemTime{ 0.0 };
        size_t totalAllocs{ 0 };
        size_t totalBytes{ 0 };
    };

public:
    // 1. ϵͳԤ��
    static void warmup()
    {
        std::cout << "Warming up memory systems...\n";
        // ʹ�� pair ���洢ָ��Ͷ�Ӧ�Ĵ�С
        std::vector<std::pair<void*, size_t>> warmupPtrs;

        // Ԥ���ڴ��
        for (int i = 0; i < 1000; ++i)
        {
            for (size_t size : {8, 16, 32, 64, 128, 256, 512, 1024}) {
                void* p = MemoryPool::allocate(size);
                warmupPtrs.emplace_back(p, size);  // �洢ָ��Ͷ�Ӧ�Ĵ�С
            }
        }

        // �ͷ�Ԥ���ڴ�
        for (const auto& [ptr, size] : warmupPtrs)
        {
            MemoryPool::deallocate(ptr, size);  // ʹ��ʵ�ʷ���Ĵ�С�����ͷ�
        }

        std::cout << "Warmup complete.\n\n";
    }

    // 2. С����������
    static void testSmallAllocation()
    {
        constexpr size_t NUM_ALLOCS = 50000;
        // ʹ�ù̶��ļ���С�����С����Щ��С�����ڴ�����Ż��Ĵ�С
        const size_t SIZES[] = { 8, 16, 32, 64, 128, 256 };
        const size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

        std::cout << "\nTesting small allocations (" << NUM_ALLOCS
            << " allocations of fixed sizes):" << std::endl;

        // �����ڴ��
        {
            Timer t;
            // ����С����洢�ڴ��
            std::array<std::vector<std::pair<void*, size_t>>, NUM_SIZES> sizePtrs;
            for (auto& ptrs : sizePtrs) {
                ptrs.reserve(NUM_ALLOCS / NUM_SIZES);
            }

            for (size_t i = 0; i < NUM_ALLOCS; ++i)
            {
                // ѭ��ʹ�ò�ͬ��С
                size_t sizeIndex = i % NUM_SIZES;
                size_t size = SIZES[sizeIndex];
                void* ptr = MemoryPool::allocate(size);
                sizePtrs[sizeIndex].push_back({ ptr, size });

                // ģ����ʵʹ�ã����������ͷ�
                if (i % 4 == 0)
                {
                    // ���ѡ��һ����С�������ͷ�
                    size_t releaseIndex = rand() % NUM_SIZES;
                    auto& ptrs = sizePtrs[releaseIndex];

                    if (!ptrs.empty())
                    {
                        MemoryPool::deallocate(ptrs.back().first, ptrs.back().second);
                        ptrs.pop_back();
                    }
                }
            }

            // ����ʣ���ڴ�
            for (auto& ptrs : sizePtrs)
            {
                for (const auto& [ptr, size] : ptrs)
                {
                    MemoryPool::deallocate(ptr, size);
                }
            }

            std::cout << "Memory Pool: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }

        // ����new/delete
        {
            Timer t;
            std::array<std::vector<std::pair<void*, size_t>>, NUM_SIZES> sizePtrs;
            for (auto& ptrs : sizePtrs) {
                ptrs.reserve(NUM_ALLOCS / NUM_SIZES);
            }

            for (size_t i = 0; i < NUM_ALLOCS; ++i)
            {
                size_t sizeIndex = i % NUM_SIZES;
                size_t size = SIZES[sizeIndex];
                void* ptr = new char[size];
                sizePtrs[sizeIndex].push_back({ ptr, size });

                if (i % 4 == 0)
                {
                    size_t releaseIndex = rand() % NUM_SIZES;
                    auto& ptrs = sizePtrs[releaseIndex];

                    if (!ptrs.empty())
                    {
                        delete[] static_cast<char*>(ptrs.back().first);
                        ptrs.pop_back();
                    }
                }
            }

            for (auto& ptrs : sizePtrs)
            {
                for (const auto& [ptr, size] : ptrs)
                {
                    delete[] static_cast<char*>(ptr);
                }
            }

            std::cout << "New/Delete: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }
    }

    // 3. ���̲߳���
    static void testMultiThreaded()
    {
        constexpr size_t NUM_THREADS = 4;
        constexpr size_t ALLOCS_PER_THREAD = 25000;

        std::cout << "\nTesting multi-threaded allocations (" << NUM_THREADS
            << " threads, " << ALLOCS_PER_THREAD << " allocations each):"
            << std::endl;

        auto threadFunc = [](bool useMemPool)
            {
                std::random_device rd;
                std::mt19937 gen(rd());

                // ʹ�ù̶��ļ�����С���������Ը��õز����ڴ�صĸ�������
                const size_t SIZES[] = { 8, 16, 32, 64, 128, 256 };
                const size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

                // ÿ���߳�ά���Լ����ڴ���б�����С����洢
                std::array<std::vector<std::pair<void*, size_t>>, NUM_SIZES> sizePtrs;
                for (auto& ptrs : sizePtrs) {
                    ptrs.reserve(ALLOCS_PER_THREAD / NUM_SIZES);
                }

                // ģ����ʵӦ���е��ڴ�ʹ��ģʽ
                for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
                {
                    // 1. ����׶Σ�����ʹ��ThreadCache
                    size_t sizeIndex = i % NUM_SIZES;  // ѭ��ʹ�ò�ͬ��С
                    size_t size = SIZES[sizeIndex];
                    void* ptr = useMemPool ? MemoryPool::allocate(size)
                        : new char[size];
                    sizePtrs[sizeIndex].push_back({ ptr, size });

                    // 2. �ͷŽ׶Σ������ڴ渴��
                    if (i % 100 == 0)  // ÿ100�η���
                    {
                        // ���ѡ��һ����С�����������ͷ�
                        size_t releaseIndex = rand() % NUM_SIZES;
                        auto& ptrs = sizePtrs[releaseIndex];

                        if (!ptrs.empty())
                        {
                            // �ͷŸô�С�����20%-30%���ڴ��
                            size_t releaseCount = ptrs.size() * (20 + (rand() % 11)) / 100;
                            releaseCount = std::min(releaseCount, ptrs.size());

                            for (size_t j = 0; j < releaseCount; ++j)
                            {
                                size_t index = rand() % ptrs.size();
                                if (useMemPool)
                                {
                                    MemoryPool::deallocate(ptrs[index].first, ptrs[index].second);
                                }
                                else
                                {
                                    delete[] static_cast<char*>(ptrs[index].first);
                                }
                                ptrs[index] = ptrs.back();
                                ptrs.pop_back();
                            }
                        }
                    }

                    // 3. �ڴ�ѹ�����ԣ�����CentralCache�ľ���
                    if (i % 1000 == 0)  // ÿ1000�η���
                    {
                        // ���ݵط�������ڴ棬����CentralCache�ľ���
                        std::vector<std::pair<void*, size_t>> pressurePtrs;
                        for (int j = 0; j < 50; ++j)
                        {
                            size_t size = SIZES[rand() % NUM_SIZES];
                            void* ptr = useMemPool ? MemoryPool::allocate(size)
                                : new char[size];
                            pressurePtrs.push_back({ ptr, size });
                        }

                        // �����ͷ���Щ�ڴ棬�����ڴ�صĻ���Ч��
                        for (const auto& [ptr, size] : pressurePtrs)
                        {
                            if (useMemPool)
                            {
                                MemoryPool::deallocate(ptr, size);
                            }
                            else
                            {
                                delete[] static_cast<char*>(ptr);
                            }
                        }
                    }
                }

                // ��������ʣ���ڴ�
                for (auto& ptrs : sizePtrs)
                {
                    for (const auto& [ptr, size] : ptrs)
                    {
                        if (useMemPool)
                        {
                            MemoryPool::deallocate(ptr, size);
                        }
                        else
                        {
                            delete[] static_cast<char*>(ptr);
                        }
                    }
                }
            };

        // �����ڴ��
        {
            Timer t;
            std::vector<std::thread> threads;

            for (size_t i = 0; i < NUM_THREADS; ++i)
            {
                threads.emplace_back(threadFunc, true);
            }

            for (auto& thread : threads)
            {
                thread.join();
            }

            std::cout << "Memory Pool: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }

        // ����new/delete
        {
            Timer t;
            std::vector<std::thread> threads;

            for (size_t i = 0; i < NUM_THREADS; ++i)
            {
                threads.emplace_back(threadFunc, false);
            }

            for (auto& thread : threads)
            {
                thread.join();
            }

            std::cout << "New/Delete: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }
    }

    // 4. ��ϴ�С����
    static void testMixedSizes()
    {
        constexpr size_t NUM_ALLOCS = 100000;
        // �����ڴ�ص�����ص㣬����С��Ϊ���ࣺ
        // 1. С����: �ʺ�ThreadCache
        // 2. �еȶ���: �ʺ�CentralCache
        // 3. �����: �ʺ�PageCache

        // ʹ�ù̶��Ĳ�������
        const size_t SMALL_SIZES[] = { 8, 16, 32, 64, 128 };
        const size_t MEDIUM_SIZES[] = { 256, 384, 512 };
        const size_t LARGE_SIZES[] = { 1024, 2048, 4096 };

        const size_t NUM_SMALL = sizeof(SMALL_SIZES) / sizeof(SMALL_SIZES[0]);
        const size_t NUM_MEDIUM = sizeof(MEDIUM_SIZES) / sizeof(MEDIUM_SIZES[0]);
        const size_t NUM_LARGE = sizeof(LARGE_SIZES) / sizeof(LARGE_SIZES[0]);

        std::cout << "\nTesting mixed size allocations (" << NUM_ALLOCS
            << " allocations with fixed sizes):" << std::endl;

        // �����ڴ��
        {
            Timer t;
            // ����С����洢�ڴ��
            std::array<std::vector<std::pair<void*, size_t>>, NUM_SMALL + NUM_MEDIUM + NUM_LARGE> sizePtrs;
            for (auto& ptrs : sizePtrs) {
                ptrs.reserve(NUM_ALLOCS / (NUM_SMALL + NUM_MEDIUM + NUM_LARGE));
            }

            for (size_t i = 0; i < NUM_ALLOCS; ++i)
            {
                size_t size;
                int category = i % 100;  // ʹ��ѭ�������������

                if (category < 60) {
                    // С����ѭ��ʹ�ù̶���С
                    size_t index = (i / 60) % NUM_SMALL;
                    size = SMALL_SIZES[index];
                }
                else if (category < 90) {
                    // �еȶ���ѭ��ʹ�ù̶���С
                    size_t index = (i / 30) % NUM_MEDIUM;
                    size = MEDIUM_SIZES[index];
                }
                else {
                    // �����ѭ��ʹ�ù̶���С
                    size_t index = (i / 10) % NUM_LARGE;
                    size = LARGE_SIZES[index];
                }

                void* ptr = MemoryPool::allocate(size);
                // ������sizePtrs�е�����
                size_t ptrIndex = (category < 60) ? (i / 60) % NUM_SMALL :
                    (category < 90) ? NUM_SMALL + (i / 30) % NUM_MEDIUM :
                    NUM_SMALL + NUM_MEDIUM + (i / 10) % NUM_LARGE;
                sizePtrs[ptrIndex].push_back({ ptr, size });

                // ģ����ʵ����������ͷ�һЩ�ڴ�
                if (i % 50 == 0)
                {
                    // ���ѡ��һ����С�����������ͷ�
                    size_t releaseIndex = rand() % sizePtrs.size();
                    auto& ptrs = sizePtrs[releaseIndex];

                    if (!ptrs.empty())
                    {
                        // �ͷŸô�С�����20%-30%���ڴ��
                        size_t releaseCount = ptrs.size() * (20 + (rand() % 11)) / 100;
                        releaseCount = std::min(releaseCount, ptrs.size());

                        for (size_t j = 0; j < releaseCount; ++j)
                        {
                            size_t index = rand() % ptrs.size();
                            MemoryPool::deallocate(ptrs[index].first, ptrs[index].second);
                            ptrs[index] = ptrs.back();
                            ptrs.pop_back();
                        }
                    }
                }
            }

            // ��������ʣ���ڴ�
            for (auto& ptrs : sizePtrs)
            {
                for (const auto& [ptr, size] : ptrs)
                {
                    MemoryPool::deallocate(ptr, size);
                }
            }

            std::cout << "Memory Pool: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }

        // ����new/delete
        {
            Timer t;
            std::array<std::vector<std::pair<void*, size_t>>, NUM_SMALL + NUM_MEDIUM + NUM_LARGE> sizePtrs;
            for (auto& ptrs : sizePtrs) {
                ptrs.reserve(NUM_ALLOCS / (NUM_SMALL + NUM_MEDIUM + NUM_LARGE));
            }

            for (size_t i = 0; i < NUM_ALLOCS; ++i)
            {
                size_t size;
                int category = i % 100;

                if (category < 60) {
                    size_t index = (i / 60) % NUM_SMALL;
                    size = SMALL_SIZES[index];
                }
                else if (category < 90) {
                    size_t index = (i / 30) % NUM_MEDIUM;
                    size = MEDIUM_SIZES[index];
                }
                else {
                    size_t index = (i / 10) % NUM_LARGE;
                    size = LARGE_SIZES[index];
                }

                void* ptr = new char[size];
                size_t ptrIndex = (category < 60) ? (i / 60) % NUM_SMALL :
                    (category < 90) ? NUM_SMALL + (i / 30) % NUM_MEDIUM :
                    NUM_SMALL + NUM_MEDIUM + (i / 10) % NUM_LARGE;
                sizePtrs[ptrIndex].push_back({ ptr, size });

                if (i % 50 == 0)
                {
                    size_t releaseIndex = rand() % sizePtrs.size();
                    auto& ptrs = sizePtrs[releaseIndex];

                    if (!ptrs.empty())
                    {
                        size_t releaseCount = ptrs.size() * (20 + (rand() % 11)) / 100;
                        releaseCount = std::min(releaseCount, ptrs.size());

                        for (size_t j = 0; j < releaseCount; ++j)
                        {
                            size_t index = rand() % ptrs.size();
                            delete[] static_cast<char*>(ptrs[index].first);
                            ptrs[index] = ptrs.back();
                            ptrs.pop_back();
                        }
                    }
                }
            }

            for (auto& ptrs : sizePtrs)
            {
                for (const auto& [ptr, size] : ptrs)
                {
                    delete[] static_cast<char*>(ptr);
                }
            }

            std::cout << "New/Delete: " << std::fixed << std::setprecision(3)
                << t.elapsed() << " ms" << std::endl;
        }
    }
};

int main()
{
    std::cout << "Starting performance tests..." << std::endl;

    // Ԥ��ϵͳ
    PerformanceTest::warmup();

    // ���в���
    PerformanceTest::testSmallAllocation();
    PerformanceTest::testMultiThreaded();
    PerformanceTest::testMixedSizes();

    return 0;
}