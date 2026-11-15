#include <queue>
#include <vector>
#include <map>
#include <iostream>
#include <functional>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <syncstream>
#include <condition_variable>
#include <cassert>

/**
 * @brief The number of resources for which the task system will coordinate access.
 *
 */
const uint32_t kNResources { 20 };

/**
 * @brief The number of jobs which will be executed via the task system.
 *
 */
const uint32_t kNJobs { 1000 };

/**
 * @brief The amount of time each job takes to complete.
 *
 */
const uint32_t kJobTimeMillis { 10 };

/**
 * @brief The chance (out of 100) that one thread will attempt to read one resource.
 *
 */
const uint8_t kReadChance { 20 };

/**
 * @brief If the thread definitely will read a resource, the chance (out of 100) that the same thread
 * will attempt to write to that resource.
 *
 */
const uint8_t kWriteChance { 5 };


static_assert(kReadChance <= 100 && "Read chance cannot exceed 100%");

static_assert(kWriteChance <= 100 && "Write chance cannot exceed 100%");

/**
 * @brief Container for a group of threads that continuously poll for new jobs to execute.
 *
 * Taken from PhD AP EcE's answer on stackoverflow: https://stackoverflow.com/a/32593825/5677302
 *
 */
class ThreadPool {
public:
    /**
     * @brief Initializes a number of threads per the number of cores available on this device's
     * physical processor.
     *
     */
    ThreadPool();

    /**
     * @brief Signals to all the threads that they must stop, and waits for them to join before
     * allowing this pool to be destroyed.
     *
     */
    ~ThreadPool();

    /**
     * @brief Pushes a new job to the back of this thread pool's job queue.
     *
     */
    void queueJob(const std::function<void()>& job);

    /**
     * @brief Immediately returns whether there are any pending jobs on the job queue.
     *
     */
    bool isBusy();

    /**
     * @brief Awaits the completion of all currently queued jobs before returning.
     *
     */
    void flush();

    /**
     * @brief Terminates all threads, clears the job queue, then restarts threads.
     *
     */
    void cancel();

private:
    /**
     * @brief The function run by a thread when it is not currently executing a job.
     *
     * Every iteration, a thread will await access to the thread pool's job queue, or a kill signal
     * from the coordinator.  It will return if given such a signal, or otherwise will pick up the
     * first available job when given access to the job queue.
     *
     */
    void threadLoop(std::size_t threadId);

    /**
     * @brief Whether all the threads should terminate in the current or next iteration of the
     * thread loop.
     *
     * @sa threadLoop()
     */
    bool mShouldDie { false };

    /**
     * @brief Permission to read/modify the job queue, or check whether all threads should return.
     *
     */
    std::mutex mMutexQueue {};

    /**
     * @brief The condition through which job availability or thread cancellation can be signalled.
     *
     */
    std::condition_variable mMutexCondition {};

    /**
     * @brief The live threads managed by this pool.
     *
     */
    std::vector<std::thread> mThreads {};

    /**
     * @brief Queue of jobs waiting to be consumed by the threads in the pool.
     *
     */
    std::queue<std::function<void()>> mJobs {};
};

/**
 * @brief Mock resource storage shared between threads.
 *
 */
class Resources {
public:
    /**
     * @brief The number of times each resource known to this program (identified by their index)
     * has been written to.
     *
     */
    std::array<std::size_t, kNResources> getNWrites() const { return mResources; }

    /**
     * @brief Increments the resource's write counter by 1.
     *
     */
    void write(std::size_t resourceId) {
        assert(resourceId < kNResources && "Non-existent resource referenced");
        ++mResources[resourceId];
    };

    /**
     * @brief Simply returns the current number of writes to the resource.
     *
     */
    std::size_t read(std::size_t resourceId) const {
        assert(resourceId < kNResources && "Non-existent resource referenced");
        return mResources[resourceId];
    };

    /**
     * @brief Clears the write counter for all resources.
     *
     */
    void clear() {
        for(auto& resource: mResources) {
            resource = 0;
        }
    }
private:
    /**
     * @brief Storage allocated to all resources, access to which must be coordinated.
     *
     * Each "resource" is identified by its index.
     *
     */
    std::array<std::size_t, kNResources> mResources {};
};

/**
 * @brief A task system implementation designed to improve application performance with multithreading
 * while maintaining coherent access to shared resources.
 *
 * Tasks are executed in the same order that they are declared to this system.  Each time a task writes
 * to or reads from a resource, it increments that resource's corresponding read or write counter.
 *
 * Writing threads are only given access to a resource only when all the reads on the current version of the
 * resource have taken place.  Reading threads may begin their reading as soon as the resource's write count
 * meets the required version.
 *
 * Reference: https://gdcvault.com/play/1012321/Task-based-Multithreading-How-to
 *
 */
class TaskSystem {
private:
    class SyncedResources;
public:

    class Graph;
    /**
     * @brief Full information about a resource in a particular execution "wave," containing its
     * name, read count, and write count.
     *
     */
    class RWRecord {
    public:
        /**
         * @brief Initializes a record for a single resource.
         *
         */
        RWRecord(const std::size_t resourceId): mResourceId { resourceId } {};
        /**
         * @brief Default constructor that assumes the resource's name is "0."
         *
         */
        RWRecord(): RWRecord { 0 } {}

        /**
         * @brief Returns the (integer) id of a resource.
         *
         */
        std::size_t getResourceId() const { return mResourceId; }

        /**
         * @brief Returns the number of times the resource has been written to up to
         * the current point of execution.
         *
         */
        std::size_t getNWrites() const { return mNWrites; }

        /**
         * @brief Returns the number of times the resource has been read from up to
         * the current point of execution, since its write count was incremented.
         *
         */
        std::size_t getNReads() const { return mNReads; }

    private:
        /**
         * @brief The numeric ID of a resource.
         *
         */
        std::size_t mResourceId;

        /**
         * @brief The number of writes this resource has received until now.
         *
         */
        std::size_t mNWrites {};

        /**
         * @brief The number of times the resource has been read from since its write
         * count was incremented.
         *
         */
        std::size_t mNReads {};

        friend class Graph;
        friend class SyncedResources;
    };

    /**
     * @brief The ID and write count of the resource together identify its current version, used to
     * coordinate read-write access to the resource between coordinating threads.
     *
     */
    class ResourceName {
    public:
        /**
         * @brief Creates a resource name object from an instance of its RWRecord.
         *
         */
        ResourceName(const RWRecord& record):
            mResourceId{ record.getResourceId() },
            mNWrites { record.getNWrites() }
        {}

        /**
         * @brief Gets the (integer) ID of a resource.
         *
         */
        std::size_t getResourceId() const { return mResourceId; }

        /**
         * @brief Gets the number of times this resource has been written to, also its version.
         *
         */
        std::size_t getNWrites() const { return mNWrites; }

        /**
         * @brief One resource is less than another if its ID is lower, or if its version is lower.
         *
         */
        bool operator<(const ResourceName& other) const {
            return (
                mResourceId < other.mResourceId || (
                    mResourceId == other.mResourceId
                    && mNWrites < other.mNWrites
                )
            );
        }

        /**
         * @brief Returns whether two names are equivalent.
         *
         */
        bool operator==(const ResourceName& other) const {
            return !(*this < other || other < *this);
        }
        /**
         * @brief Returns whether two names are not equivalent.
         *
         */
        bool operator!=(const ResourceName& other) const {
            return !(*this == other);
        }

    private:
        /**
         * @brief The (integer) id of a resource.
         *
         */
        std::size_t mResourceId;

        /**
         * @brief The number of writes this resource has received so far; its version.
         *
         */
        std::size_t mNWrites;
    };

    class Task;

    /**
     * @brief Container holding read-write records for resources across all waves of multithreaded
     * execution, as well as a list of tasks in the overall order they should be executed.
     *
     */
    class Graph {
    public:
        /**
         * @brief Creates a new graph object with fresh resource records and an empty task queue.
         *
         */
        Graph();


        /**
         * @brief Adds a new task, where setup enables the task to declare which resources it will be
         * reading from or writing to, and the callback actually performs the activity associated with
         * the task.
         *
         */
        void addTask(
            std::function<void(Task&)> setup,
            std::function<void()> callback
        );

        /**
         * @brief Returns the name of the latest version of a particular resource.
         *
         */
        ResourceName getResourceName(std::size_t resourceId) const {
            return ResourceName { mRWRecords[resourceId].back() };
        }

    private:

        /**
         * @brief Stores per-version read-write counts for the resources, across the entirity
         * of the task queue's execution.
         *
         */
        std::array<std::vector<RWRecord>, kNResources> mRWRecords;

        /**
         * @brief The actual list of tasks in the order they should be executed, whose execution
         * may be parallelized and whose resource access should be coordinated.
         *
         */
        std::vector<Task> mTasks {};

        friend class Task;
        friend class TaskSystem;
    };


    /**
     * @brief A single unit of work, represented as a function, that can be executed on a thread.
     *
     * Contains meta information about which version of resources it will attempt to read from or write
     * to.
     *
     */
    class Task {
    public:
        /**
         * @brief Declares that this task will read from some particular resource.
         *
         */
        void addRead(std::size_t resource);

        /**
         * @brief Declares that this task will write to a particular resource.
         *
         */
        void addWrite(std::size_t resource);

    private:
        /**
         * @brief Call the function this task was initialized with.
         *
         */
        void execute() { mCallback(); };

        /**
         * @brief Initializes this task with the function it should execute, and the graph
         * that owns it.
         *
         */
        Task(std::function<void()>& callback, Graph& graph);

        /**
         * @brief The function that will be executed when this task is scheduled to a thread.
         *
         */
        std::function<void()> mCallback;

        /**
         * @brief A reference to this task's parent graph.
         *
         */
        Graph& mGraph;

        /**
         * @brief A list of resources this task will (false) read from or (true) write to.
         *
         */
        std::map<ResourceName, bool> mAccessedResources {};

        friend class Graph;
        friend class TaskSystem;
        friend class SyncedResources;
    };


    /**
     * @brief Takes ownership of a graph, begins its execution on a thread pool, and coordinates
     * access to shared resources between threads.
     *
     */
    void execute(Graph&& graph, Resources& resources, ThreadPool& threadPool);

private:
    /**
     * @brief Pointer to the last task graph executed by the system.
     *
     */
    std::unique_ptr<Graph> mCurrentGraph { nullptr };

    /**
     * @brief Resource reference with additional synchronization information, to coordinate
     * access to shared resources between threads.
     *
     */
    std::unique_ptr<SyncedResources> mResources { nullptr };

    /**
     * @brief Resource reference with additional synchronization information, to coordinate
     * access to shared resources between threads.
     *
     */
    class SyncedResources {
    public:
        /**
         * @brief Initializes object with shared resource storage reference, and information about each expected version
         * of each resource.
         *
         */
        SyncedResources(Resources& resources, const std::array<std::vector<RWRecord>, kNResources>& expectedAccesses);

        /**
         * @brief Waits for read/write access to each resource depended on by a task.
         *
         */
        void awaitAccess(const Task& task);

        /**
         * @brief Increments read/write counter and notifies waiting threads.
         *
         */
        void signalExit(const Task& task);

    private:
        /**
         * @brief Reference to shared resource storage to which access must be coordinated.
         *
         */
        Resources& mResources;

        /**
         * @brief The read-write information expected for each resource throughout its lifetime during
         * the execution of the current task graph.
         *
         */
        std::array<std::vector<RWRecord>, kNResources> mExpected;

        /**
         * @brief The current latest read-write information about each resource.
         *
         */
        std::array<RWRecord, kNResources> mRecords {};

        /**
         * @brief Mutexes protecting access to each resource.
         *
         */
        std::array<std::shared_mutex, kNResources> mMutexes {};

        /**
         * @brief Condition variable through which resource record can be checked by each executing
         * thread.
         *
         */
        std::array<std::condition_variable_any, kNResources> mConditionVariables {};
    };

    /**
     * @brief Performs a single task with the appropriate synchronization actions taken before
     * and after the core function.
     *
     */
    void executeTask(Task& task, SyncedResources& resources);
};

/**
 * @brief Driver program that initializes a pool of threads, compares performance of the same
 * series of functions in both single-threaded and multi-threaded modes.
 *
 * Initializes a total of kNJobs jobs, each of which have a kReadChance chance to read from a shared
 * resource, and a kReadChance * kWriteChance chance to write to that resource.  Initializes a thread
 * pool, each thread of which pools for a job to complete.
 *
 * Executes the jobs in linear order, measuring time to completion (where each job takes kJobTimeMillis ms)
 * and reports it to the console.  Repeats the same using the task system and multithreading.
 *
 */
int main() {
    ThreadPool pool {};

    Resources resources {};

    std::vector<std::pair<std::function<void(TaskSystem::Task&)>, std::function<void()>>> jobs {};

    for(auto i { 0 }; i < kNJobs; ++i) {
        std::array<std::pair<bool, bool>, kNResources> readOrWrite {};
        for(auto& decision: readOrWrite) {
            decision.first = (std::rand() % 100) > (100 - kReadChance);
            decision.second = (std::rand() % 100) > (100 - kWriteChance);
        }
        jobs.push_back({
            [=](TaskSystem::Task& task) {
                for(auto j { 0 }; j < kNResources; ++j) {
                    if(!readOrWrite[j].first) {
                        continue;
                    }

                    if(readOrWrite[j].second) {
                        task.addWrite(j);
                    } else {
                        task.addRead(j);
                    }
                }
            },
            [=, &resources](){
                std::this_thread::sleep_for(std::chrono::milliseconds(kJobTimeMillis));
                for(auto j { 0 }; j < kNResources; ++j) {
                    if(!readOrWrite[j].first) {
                        continue;
                    }

                    if(readOrWrite[j].second) {
                        resources.write(j);
                    } else {
                        resources.read(j);
                    }
                }
            }
        });
    }
    const auto singleThreadedStart { std::chrono::system_clock::now() };
    for(auto& job: jobs) {
        job.second();
    }
    const auto singleThreadedEnd { std::chrono::system_clock::now() };
    const auto singleThreadedTime { singleThreadedEnd - singleThreadedStart };

    std::cout << "Single threaded time: " << std::chrono::duration_cast<std::chrono::milliseconds>(singleThreadedTime).count() << "ms\n";
    std::cout << "Single threaded write counts: ";
    for(auto& nWrites: resources.getNWrites()) {
        std::cout << nWrites << ", ";
    }
    std::cout << "\n";

    resources.clear();
    TaskSystem taskSystem {};
    TaskSystem::Graph graph {};
    const auto multiThreadedStart { std::chrono::system_clock::now() };
    for(auto& job: jobs) {
        graph.addTask(job.first, job.second);
    }
    taskSystem.execute(std::move(graph), resources, pool);
    pool.flush();
    const auto multiThreadedEnd { std::chrono::system_clock::now() };
    const auto multiThreadedTime { multiThreadedEnd - multiThreadedStart };

    std::cout << "Multi threaded time: " << std::chrono::duration_cast<std::chrono::milliseconds>(multiThreadedTime).count() << "ms\n";
    std::cout << "Multi threaded write counts: ";
    for(auto& nWrites: resources.getNWrites()) {
        std::cout << nWrites << ", ";
    }
    std::cout << "\n";

    return 0;
}

ThreadPool::ThreadPool() {
    const std::size_t nMaxThreads { std::thread::hardware_concurrency() };
    for(auto i { 0 }; i < nMaxThreads; ++i) {
        mThreads.emplace_back(std::thread { &ThreadPool::threadLoop, this, i });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mMutexQueue);
        mShouldDie = true;
    }
    mMutexCondition.notify_all();
    for(auto& thread: mThreads) {
        thread.join();
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

void ThreadPool::flush() {
    while(!mJobs.empty()) { /* await completion of all currently queued jobs */ };
    cancel();
}

void ThreadPool::cancel() {
    const std::size_t nMaxThreads { std::thread::hardware_concurrency() };
    {
        std::unique_lock<std::mutex> lock(mMutexQueue);
        mShouldDie = true;
    }
    mMutexCondition.notify_all();

    for(std::thread& thread: mThreads) {
        thread.join();
    }

    mShouldDie = false;
    while(!mJobs.empty()) {
        mJobs.pop();
    }

    for(auto i { 0 }; i < nMaxThreads; ++i) {
        mThreads[i] = std::thread { &ThreadPool::threadLoop, this, i };
    }
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
    for(auto& resource: task.mAccessedResources) {
        const bool willWrite { resource.second };
        const std::size_t resourceId { resource.first.getResourceId() };

        std::shared_lock<std::shared_mutex> lock { mMutexes[resourceId] };
        assert(lock.owns_lock() && "Failed to acquire shared lock");
        mConditionVariables[resourceId].wait(lock, [&, this]() {
                const RWRecord recordCurrent { mRecords[resourceId] };
                if(ResourceName { recordCurrent } != resource.first) {
                    return false;
                }

                const auto nExpectedReads {
                    this->mExpected[resourceId][resource.first.getNWrites()].getNReads()
                };
                if(willWrite && recordCurrent.mNReads != nExpectedReads) {
                    return false;
                }
            return true;
        });
    }
}

void TaskSystem::SyncedResources::signalExit(const Task& task) {
    for(auto& resource: task.mAccessedResources) {
        const std::size_t resourceId { resource.first.getResourceId() };
        const bool didWrite { resource.second };
        {
            std::unique_lock<std::shared_mutex> lock { mMutexes[resourceId] };
            assert(lock.owns_lock() && "Failed to acquire unique lock");
            if(!didWrite) {
                ++mRecords[resourceId].mNReads;
            } else {
                ++mRecords[resourceId].mNWrites;
                mRecords[resourceId].mNReads = 0;
            }
        }
        mConditionVariables[resourceId].notify_all();
    }
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

