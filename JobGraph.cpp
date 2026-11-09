#include <vector>
#include <iostream>
#include <thread>

void sayPathetic(std::size_t threadId) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << threadId << " is pathetic.\n";
}

int main() {

    const std::size_t nMaxThreads { std::thread::hardware_concurrency() };
    std::cout << "Maximum " << nMaxThreads << " allowed.\n";
    std::vector<std::thread> threads {};

    for(auto i { 0 }; i < nMaxThreads; ++i) {
        threads.push_back(std::thread { &sayPathetic, i });
    }

    for(auto& thread: threads) {
        thread.join();
    }

    return 0;
}

