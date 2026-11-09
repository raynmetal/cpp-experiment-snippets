#include <queue>
#include <vector>
#include <iostream>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

// Taken from PhD AP EcE's answer on stackoverflow:
// - https://stackoverflow.com/a/32593825/5677302
class ThreadPool {
public:
    void start();
    void queueJob(const std::function<void()>& job);
    void stop();
    bool isBusy();

private:
    void threadLoop(std::size_t threadId);

    bool mShouldDie { false };
    std::mutex mMutexQueue {};
    std::condition_variable mMutexCondition {};
    std::vector<std::thread> mThreads {};
    std::queue<std::function<void()>> mJobs {};
};

void sayPathetic(std::size_t jobId);


int main() {
    ThreadPool pool {};
    pool.start();

    const std::size_t nJobs { 10000 };
    std::cout << "Running " << nJobs << " jobs\n";

    for(auto i { 0 }; i < nJobs; ++i) {
        pool.queueJob(std::bind(sayPathetic, i));
    }

    // wait for all jobs to be completed, then stop
    while(pool.isBusy());
    pool.stop();

    return 0;
}

void sayPathetic(std::size_t jobId) {
    std::cout << "... working on job " << jobId << " ...\n";
}


void ThreadPool::start() {
    const std::size_t nMaxThreads { std::thread::hardware_concurrency() };
    std::cout << "Initializing " << nMaxThreads << " threads\n";
    for(auto i { 0 }; i < nMaxThreads; ++i) {
        mThreads.emplace_back(std::thread { &ThreadPool::threadLoop, this, i });
    }
}

void ThreadPool::threadLoop(std::size_t threadId) {
    while(true) {

        std::function<void()> job;
        {
            // receive read/write permission for the jobs queue
            std::cout << threadId << " is waiting for queue permission.\n";
            std::unique_lock<std::mutex> lock { mMutexQueue };

            // either wait for the signal telling us to die, or pick
            // up the next job in the queue
            std::cout << threadId << " is waiting for a job to be scheduled.\n";
            mMutexCondition.wait(lock, [this] {
                return !mJobs.empty() || mShouldDie;
            });

            if(mShouldDie) {
                std::cout << threadId << " received die signal.\n";
                return;
            }

            job = mJobs.front();
            mJobs.pop();
        }

        // execute the job, then wait for the next one
        std::cout << threadId << " is executing job.\n";
        job();
    }
}

void ThreadPool::queueJob(const std::function<void()>& job) {
    {
        std::unique_lock<std::mutex> lock { mMutexQueue };
        mJobs.push(job);
    }
    mMutexCondition.notify_one();
}

bool ThreadPool::isBusy() {
    bool busy;
    {
        std::unique_lock<std::mutex> lock { mMutexQueue };
        busy = !mJobs.empty();
    }
    return busy;
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(mMutexQueue);
        mShouldDie = true;
    }
    mMutexCondition.notify_all();
    for(std::thread& thread: mThreads) {
        thread.join();
    }
    mThreads.clear();
}
