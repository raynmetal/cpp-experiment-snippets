#include <queue>
#include <vector>
#include <map>
#include <iostream>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cassert>

const std::size_t kNResources { 8 };

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

class Resources {
public:

    void write(std::size_t resourceId) {
        assert(resourceId < kNResources && "Non-existent resource referenced");
        ++mResources[resourceId];
    };

    std::size_t read(std::size_t resourceId) const {
        assert(resourceId < kNResources && "Non-existent resource referenced");
        return mResources[resourceId];
    };

private:
    std::array<std::size_t, kNResources> mResources {};
};

// Reference: https://gdcvault.com/play/1012321/Task-based-Multithreading-How-to
class TaskSystem {
private:
    class SyncedResources;
public:

    class Graph;
    class RWRecord {
    public:
        RWRecord(const std::size_t resourceId): mResourceId { resourceId } {};
        RWRecord(): RWRecord { 0 } {}
        std::size_t getResourceId() const { return mResourceId; }
        std::size_t getNWrites() const { return mNWrites; }
        std::size_t getNReads() const { return mNReads; }

    private:
        std::size_t mResourceId;
        std::size_t mNWrites {};
        std::size_t mNReads {};

        friend class Graph;
        friend class SyncedResources;
    };

    class ResourceName {
    public:
        ResourceName(const RWRecord& record):
            mResourceId{ record.getResourceId() },
            mNWrites { record.getNWrites() }
        {}

        std::size_t getResourceId() const { return mResourceId; }
        std::size_t getNWrites() const { return mNWrites; }

        bool operator<(const ResourceName& other) const {
            return (
                mResourceId < other.mResourceId || (
                    mResourceId == other.mResourceId
                    && mNWrites < other.mNWrites
                )
            );
        }
        bool operator==(const ResourceName& other) const {
            return !(*this < other || other < *this);
        }
        bool operator!=(const ResourceName& other) const {
            return !(*this == other);
        }

    private:

        std::size_t mResourceId;
        std::size_t mNWrites;
    };

    class Task;

    class Graph {
    public:
        Graph();


        void addTask(
            std::function<void(Task&)> setup,
            std::function<void()> callback
        );

        ResourceName getResourceName(std::size_t resourceId) const {
            return ResourceName { mRWRecords[resourceId].back() };
        }

    private:

        std::array<std::vector<RWRecord>, kNResources> mRWRecords;
        std::vector<Task> mTasks {};

        friend class Task;
        friend class TaskSystem;
    };


    class Task {
    public:
        void addRead(std::size_t resource);
        void addWrite(std::size_t resource);

    private:
        void execute() { mCallback(); };
        Task(std::function<void()>& callback, Graph& graph);

        std::function<void()> mCallback;
        Graph& mGraph;

        // true if written to, false otherwise
        std::map<ResourceName, bool> mAccessedResources {};

        friend class Graph;
        friend class TaskSystem;
        friend class SyncedResources;
    };


    void execute(Graph&& graph, Resources& resources, ThreadPool& threadPool);

private:
    std::unique_ptr<Graph> mCurrentGraph { nullptr };
    std::unique_ptr<SyncedResources> mResources { nullptr };

    class SyncedResources {
    public:
        SyncedResources(Resources& resources, const std::array<std::vector<RWRecord>, kNResources>& expectedAccesses);
        void awaitAccess(const Task& task);
        void signalExit(const Task& task);

    private:
        Resources& mResources;
        std::array<std::vector<RWRecord>, kNResources> mExpected;
        std::array<RWRecord, kNResources> mRecords {};
        std::mutex mMutex {};
        std::condition_variable mConditionVariable {};
    };

    void executeTask(Task& task, SyncedResources& resources);
};

int main() {
    ThreadPool pool {};
    pool.start();

    Resources resources {};
    TaskSystem taskSystem {};
    TaskSystem::Graph graph {};
    graph.addTask(
        [&](TaskSystem::Task& task) {
            task.addWrite(0);
        },
        [=]() {
            std::cout << "executing A.\n";
        }
    );
    graph.addTask(
        [&](TaskSystem::Task& task) {
            task.addRead(0);
        },
        [=]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "executing B.\n";
        }
    );
    graph.addTask(
        [&](TaskSystem::Task& task) {
            task.addRead(0);
        },
        [=]() {
            std::cout << "executing C.\n";
        }
    );
    graph.addTask(
        [&](TaskSystem::Task& task) {
            task.addWrite(0);
        },
        [=]() {
            std::cout << "executing D.\n";
        }
    );
    taskSystem.execute(std::move(graph), resources, pool);

    // wait for all jobs to be completed, then stop
    while(pool.isBusy());
    pool.stop();

    return 0;
}

void ThreadPool::start() {
    const std::size_t nMaxThreads { std::thread::hardware_concurrency() };
    for(auto i { 0 }; i < nMaxThreads; ++i) {
        mThreads.emplace_back(std::thread { &ThreadPool::threadLoop, this, i });
    }
}

void ThreadPool::threadLoop(std::size_t threadId) {
    while(true) {

        std::function<void()> job;
        {
            // receive read/write permission for the jobs queue
            std::unique_lock<std::mutex> lock { mMutexQueue };

            // either wait for the signal telling us to die, or pick
            // up the next job in the queue
            mMutexCondition.wait(lock, [this] {
                return !mJobs.empty() || mShouldDie;
            });

            if(mShouldDie) {
                return;
            }

            job = mJobs.front();
            mJobs.pop();
        }

        // execute the job, then wait for the next one
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

TaskSystem::Graph::Graph() {
    // Initialize every stack with one element
    for(std::size_t i {0}; i < kNResources; ++i) {
        mRWRecords[i].push_back(TaskSystem::RWRecord { i });
    }
}

void TaskSystem::Graph::addTask(
    std::function<void(Task&)> setup,
    std::function<void()> callback
) {
    Task task { callback, *this };
    setup(task);

    for(auto& resource: task.mAccessedResources) {
        const std::size_t resourceId { resource.first.getResourceId() };

        // if this is a read only operation, only the read count changes
        if(!resource.second) {
            mRWRecords[resourceId].back().mNReads++;

        // a write operation pushes a new record to the stack
        } else {
            RWRecord record { mRWRecords[resourceId].back() };
            ++record.mNWrites;
            record.mNReads = 0;
            mRWRecords[resourceId].push_back(record);
        }
    }

    mTasks.push_back(task);
}

TaskSystem::Task::Task(std::function<void()>& callback, TaskSystem::Graph& graph):
    mCallback { callback }, mGraph { graph }
{}

void TaskSystem::Task::addRead(const std::size_t resourceId) {
    assert(resourceId < kNResources && "Non-existent resource referenced");
    const ResourceName resource { mGraph.getResourceName(resourceId) };
    assert(mAccessedResources.find(resource) == mAccessedResources.end() && "This resource's access has already been specified");
    mAccessedResources[resource] = false;
}

void TaskSystem::Task::addWrite(const std::size_t resourceId) {
    assert(resourceId < kNResources && "Non-existent resource referenced");
    const ResourceName resource { mGraph.getResourceName(resourceId) };
    assert(mAccessedResources.find(resource) == mAccessedResources.end() && "This resource's access has already been specified");
    mAccessedResources[resource] = true;
}

TaskSystem::SyncedResources::SyncedResources(Resources& resources, const std::array<std::vector<TaskSystem::RWRecord>, kNResources>& expectedAccesses):
    mResources { resources },
    mExpected { expectedAccesses }
{
    for(auto i { 0 }; i < kNResources; ++i) {
        mRecords[i].mResourceId = i;
    }
}

void TaskSystem::SyncedResources::awaitAccess(const Task& task) {
    std::unique_lock<std::mutex> lock { mMutex };
    mConditionVariable.wait(lock, [&, this]() {
        for(auto& resource: task.mAccessedResources) {
            const bool willWrite { resource.second };
            const std::size_t resourceId { resource.first.getResourceId() };
            const RWRecord recordCurrent { this->mRecords[resourceId] };
            if(ResourceName { recordCurrent } != resource.first) {
                return false;
            }

            const auto nExpectedReads {
                this->mExpected[resourceId][resource.first.getNWrites()].getNReads()
            };
            if(willWrite && recordCurrent.mNReads != nExpectedReads) {
                return false;
            }
        }
        return true;
    });
}

void TaskSystem::SyncedResources::signalExit(const Task& task) {
    {
        std::unique_lock<std::mutex> lock { mMutex };
        for(auto& resource: task.mAccessedResources) {
            const std::size_t resourceId { resource.first.getResourceId() };
            const bool didWrite { resource.second };
            if(!didWrite) {
                ++mRecords[resourceId].mNReads;
            } else {
                ++mRecords[resourceId].mNWrites;
                mRecords[resourceId].mNReads = 0;
            }
        }
    }

    mConditionVariable.notify_all();
}

void TaskSystem::executeTask(TaskSystem::Task& task, SyncedResources& resources) {
    resources.awaitAccess(task);
    task.execute();
    resources.signalExit(task);
}

void TaskSystem::execute(Graph&& graph, Resources& resources, ThreadPool& pool) {
    while(pool.isBusy()) { /* wait for the previous batch of tasks to end before we do anything */ }

    // make this our actively executing graph
    mCurrentGraph = std::make_unique<Graph>(std::move(graph));
    mResources = std::make_unique<SyncedResources>(resources, mCurrentGraph->mRWRecords);
    for(auto& task: mCurrentGraph->mTasks) {
        pool.queueJob(std::bind(
            &TaskSystem::executeTask, this,
            std::ref(task),
            std::ref(*mResources)
        ));
    }
}
