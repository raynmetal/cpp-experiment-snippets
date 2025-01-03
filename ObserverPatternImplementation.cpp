#include <functional>
#include <memory>
#include <map>
#include <cassert>
#include <set>
#include <iostream>

class SignalTracker;
class ISignalObserver;
class ISignal;
template <typename ...TArgs>
class SignalObserver_;
template <typename ...TArgs>
class Signal_;
template <typename ...TArgs>
class Signal;
template <typename ...TArgs>
class SignalObserver;

class ISignalObserver {};
class ISignal {
public:
    virtual void registerObserver(std::weak_ptr<ISignalObserver> observer)=0;
};

template <typename ...TArgs>
class Signal_: public ISignal {
public:
    void emit (TArgs... args);
    void registerObserver(std::weak_ptr<ISignalObserver> observer) override;

private:
    // creation managed through SignalTracker
    Signal_() = default;

    std::set<
        std::weak_ptr<SignalObserver_<TArgs...>>,
        std::owner_less<std::weak_ptr<SignalObserver_<TArgs...>>>
    > mObservers {};

friend class SignalTracker;
friend class Signal<TArgs...>;
};

template <typename ...TArgs>
class SignalObserver_: public ISignalObserver {
public:
    void operator() (TArgs... args);
private:
    SignalObserver_(std::function<void(TArgs...)> callback);
    std::function<void(TArgs...)> mStoredFunction {};
friend class SignalTracker;
friend class SignalObserver<TArgs...>;
};

class SignalTracker {
public:
    SignalTracker() = default;
    // let copy constructor just create its own signal list
    SignalTracker(const SignalTracker& other): SignalTracker{} {}
    // copy assignment too
  
    SignalTracker& operator=(const SignalTracker& other) {
        // Note: There is no point doing any work in this
        // case, as it is the responsibility of the inheritor
        // to make sure signals and connections are correctly
        // reconstructed. Dead signals and observers will 
        // automatically be cleaned up
        return *this;
    }

    SignalTracker(SignalTracker&& other): SignalTracker{} {}
    SignalTracker& operator=(SignalTracker&& other) {
        // Note: same as in copy assignment
        return *this;
    }

    void connect(const std::string& theirSignal, const std::string& ourObserver, SignalTracker& other);

private:
    template <typename ...TArgs>
    std::shared_ptr<Signal_<TArgs...>> declareSignal(
        const std::string& signalName
    );

    template <typename ...TArgs>
    std::shared_ptr<SignalObserver_<TArgs...>> declareSignalObserver(
        const std::string& observerName,
        std::function<void(TArgs...)> callbackFunction 
    );

    void garbageCollection();
    std::unordered_map<std::string, std::weak_ptr<ISignalObserver>> mObservers {};
    std::unordered_map<std::string, std::weak_ptr<ISignal>> mSignals {};
friend class ISignal;
friend class IObserver;

template <typename ...TArgs>
friend class SignalObserver;

template <typename ...TArgs>
friend class Signal;
};

template <typename ...TArgs>
class Signal {
public:
    Signal(SignalTracker& owningTracker, const std::string& name) {
        resetSignal(owningTracker, name);
    }

    Signal(const Signal& other) = delete;
    Signal(Signal&& other) = delete;
    Signal& operator=(const Signal& other) = delete;
    Signal& operator=(Signal&& other) = delete;

    void emit(TArgs...args) { mSignal_->emit(args...); }
    void resetSignal(SignalTracker& owningTracker, const std::string& name) {
        mSignal_ = owningTracker.declareSignal<TArgs...>(name);
    }


private:
    void registerObserver(SignalObserver<TArgs...>& observer) {
        mSignal_->registerObserver(observer.mSignalObserver_);
    }

    std::shared_ptr<Signal_<TArgs...>> mSignal_;
 
friend class SignalObserver<TArgs...>;
};


template <typename ...TArgs>
class SignalObserver {
public:
    SignalObserver(SignalTracker& owningTracker, const std::string& name, std::function<void(TArgs...)> callback) {
        resetObserver(owningTracker, name, callback);
    };

    SignalObserver(const SignalObserver& other)=delete;
    SignalObserver(SignalObserver&& other)=delete;
    SignalObserver& operator=(const SignalObserver& other) = delete;
    SignalObserver& operator=(SignalObserver&& other) = delete;

    void resetObserver(SignalTracker& owningTracker, const std::string& name, std::function<void(TArgs...)> callback) {
        assert(callback && "Empty callback is not allowed");
        mSignalObserver_ = owningTracker.declareSignalObserver<TArgs...>(name, callback);
    }

    void connect(Signal<TArgs...>& signal) {
        signal.registerObserver(*this);
    }
 
private:
    std::shared_ptr<SignalObserver_<TArgs...>> mSignalObserver_;
 
friend class Signal<TArgs...>;
};


class A {};

class B: public A, public SignalTracker {
public:
    B()=default;

    // Copy construction and assignment should be defined explicitly,
    // since Signal is not moveable or copyable
    B(const B& other): B{} {}
    B& operator=(const B& other) {/* nothing to copy */ return *this; }

    void doSomething(int thingToDo) { 
        std::cout << "B is doing something: " << thingToDo << "\n";
        const int thingDone { thingToDo };
        sigDidSomething.emit(thingDone);
    }

    Signal<int> sigDidSomething { *this, "somethingDone" };
protected:
};

class C {
    // TODO: SIGFPE occurs if mSignalTracker is declared after sigDidSomething, which depends 
    // on it for its initialization. 
    //
    // Is there a good way to make this less brittle, or otherwise at least
    // to make this relationship clear to users?
    SignalTracker mSignalTracker {};
public:
    void doSomething(int thingToDo) {
        std::cout << "C is doing something: " << thingToDo << "\n";
        const int thingDone { thingToDo };
        sigDidSomething.emit(thingDone);
    }

    Signal<int> sigDidSomething { mSignalTracker, "somethingDone" };
};

class P: public SignalTracker {
public: 
    P()=default;

    // construction and assignment should be defined
    // explicitly, as SignalObserver is a non-copyable,
    // non-moveable type
    P(const P& other): P{} {}
    P& operator=(const P& other) {/* nothing to copy */ return *this;}

    SignalObserver<int> somethingDoneObserver {
        *this,
        "somethingDone",
        {[this](int thingThatWasDone) { this->somethingDoneCallback(thingThatWasDone); }}
    };

protected:
    void somethingDoneCallback(int thingThatWasDone) {
        std::cout << "P: someone was heard doing something: " << thingThatWasDone << std::endl;
    }
};

class Q {
    // TODO: SIGFPE occurs if mSignalTracker is declared after somethingDoneObserver, which depends 
    // on it for its initialization. 
    //
    // Is there a good way to make this less brittle, or otherwise at least
    // to make this relationship clear to users?
    SignalTracker mSignalTracker {};

public:
    void didSomethingCallback(int thingDone) {
        std::cout << "Q: someone was heard doing something: " << thingDone << "\n";
    }

    SignalObserver<int> somethingDoneObserver { 
        this->mSignalTracker,
        "somethingDone",
        {[this](int thing) { this->didSomethingCallback(thing); }}
    };

private:
};

int main() {
    std::shared_ptr<B> ptrB { std::make_shared<B>() };

    // 0) No observer, single subject
    // B presently has no observers, so only 1 line printed
    ptrB->doSomething(0);
    std::cout << "\n";

    // 1) Single observer, single subject
    // one listener, and so one additional line printed (total 2)
    {
        std::shared_ptr<P> ptrP { std::make_shared<P>() };
        ptrP->connect("somethingDone", "somethingDone", *ptrB);
        ptrB->doSomething(1);
    }
    std::cout  << "\n";

    // 2) Observer removed, single subject
    // no more pointer to P, again only 1 line printed
    ptrB->doSomething(2);
    std::cout << "\n";

    // 3) Multiple observers, single subject
    // total of 6 lines, with 5 from observers
    {
        std::vector<std::shared_ptr<P>> multiplePs {};
        for(int i{0}; i < 5; ++i) {
            multiplePs.push_back(std::make_shared<P>());
            multiplePs.back()->connect("somethingDone", "somethingDone", *ptrB);
        }
        ptrB->doSomething(3);
    }
    std::cout << "\n";

    // 4) Single observer, multiple subjects
    // Signal from each B heard by a single observer, making no.
    // of lines printed 2x the number of emitters (5 observers, 
    // so 10 lines total)
    {
        std::vector<std::shared_ptr<B>> multipleBs {};
        std::shared_ptr<P> singleP {std::make_shared<P>()};
        for(int i{0}; i < 5; ++i) {
            multipleBs.push_back(std::make_shared<B>());
            singleP->connect("somethingDone", "somethingDone", *multipleBs.back());
        }

        for(auto& b: multipleBs) {
            b->doSomething(4);
        }
    }
    std::cout << "\n";

    // 5) Single observer, 2 subjects with one constructed by copy (total 3 lines)
    {
        std::shared_ptr<P> singleP { std::make_shared<P>() };

        singleP->connect("somethingDone", "somethingDone", *ptrB);

        // the signal from ptrB is not actually copied over to
        // copyB. Instead, copyB makes its own version of the 
        // signal using its default constructor
        std::shared_ptr<B> copyB { std::make_shared<B>(*ptrB) };

        // has connection to singleP, so 2 lines
        ptrB->doSomething(5);

        // no connection to singleP, just 1 line
        copyB->doSomething(5);
    }
    std::cout << "\n";

    // 6) 2 Observers with one constructed by copy after connection, 1 subject (total 2 lines)
    {
        std::shared_ptr<P> firstP { std::make_shared<P>() };
        firstP->connect("somethingDone", "somethingDone", *ptrB);
        std::shared_ptr<P> secondP { std::make_shared<P>(*firstP) };

        // secondP didn't copy connection to ptrB, so only 2 lines 
        ptrB->doSomething(6);
    }
    std::cout << "\n";

    // 7) Single observer, single subject, this time connecting them through interface
    // provided by their SignalObserver member (total 2 lines)
    {
        std::shared_ptr<P> singleP { std::make_shared<P>() };
        singleP->somethingDoneObserver.connect(ptrB->sigDidSomething);
        ptrB->doSomething(7);
    }
    std::cout << "\n";

    // 8) Single observer, single subject, but observer holds SignalTracker as member
    // rather than inheriting from it (total 2 lines)
    {
        std::shared_ptr<Q> singleQ { std::make_shared<Q>() };
        singleQ->somethingDoneObserver.connect(ptrB->sigDidSomething);
        ptrB->doSomething(8);
    }
    std::cout << "\n";

    // 9) Single observer, single subject, but subject holds Signal as member
    // rather than inheriting from it (total 2 lines)
    {
        std::shared_ptr<C> singleC { std::make_shared<C>() };
        std::shared_ptr<P> singleP { std::make_shared<P>() };
        singleP->somethingDoneObserver.connect(singleC->sigDidSomething);
        singleC->doSomething(9);
    }
    std::cout << "\n";

    return 0;
}

template <typename ...TArgs>
inline void Signal_<TArgs...>::registerObserver(std::weak_ptr<ISignalObserver> observer) {
    assert(!observer.expired() && "Cannot register a null pointer as an observer");
    mObservers.insert(std::static_pointer_cast<SignalObserver_<TArgs...>>(observer.lock()));
}

template <typename ...TArgs>
void Signal_<TArgs...>::emit (TArgs ... args) {
    //observers that will be removed from the list after this signal has been emitted
    std::vector<std::weak_ptr<SignalObserver_<TArgs...>>> expiredObservers {};

    for(auto observer: mObservers) {
        // lock means that this observer is still active
        if(std::shared_ptr<SignalObserver_<TArgs...>> activeObserver = observer.lock()) {
            (*activeObserver)(args...);

        // go to the purge list
        } else {
            expiredObservers.push_back(observer);
        }
    }

    // remove dead observers
    for(auto expiredObserver: expiredObservers) {
        mObservers.erase(expiredObserver);
    }
}

template <typename ...TArgs>
inline SignalObserver_<TArgs...>::SignalObserver_(std::function<void(TArgs...)> callback):
mStoredFunction{ callback }
{}

template <typename ...TArgs>
inline void SignalObserver_<TArgs...>::operator() (TArgs ... args) { 
    mStoredFunction(args...);
}

template <typename ...TArgs>
std::shared_ptr<Signal_<TArgs...>> SignalTracker::declareSignal(const std::string& name) {
    std::shared_ptr<ISignal> newSignal { new Signal_<TArgs...>{} };
    mSignals.insert({name, newSignal});
    garbageCollection();
    return std::static_pointer_cast<Signal_<TArgs...>>(newSignal);
}

template <typename ...TArgs>
std::shared_ptr<SignalObserver_<TArgs...>> SignalTracker::declareSignalObserver(const std::string& name, std::function<void(TArgs...)> callback) {
    std::shared_ptr<ISignalObserver> newObserver { new SignalObserver_<TArgs...>{callback} };
    mObservers.insert({name, newObserver});
    garbageCollection();
    return std::static_pointer_cast<SignalObserver_<TArgs...>>(newObserver);
}

inline void SignalTracker::garbageCollection() {
    std::vector<std::string> signalsToErase {};
    std::vector<std::string> observersToErase{};
    for(auto& pair: mSignals) {
        if(pair.second.expired()) { 
            signalsToErase.push_back(pair.first);
        }
    }
    for(auto& pair: mObservers) {
        if(pair.second.expired()) {
            observersToErase.push_back(pair.first);
        }
    }

    for(auto& signal: signalsToErase) {
        mSignals.erase(signal);
    }
    for(auto& observer: observersToErase) {
        mObservers.erase(observer);
    }
}

void SignalTracker::connect(const std::string& theirSignalsName, const std::string& ourObserversName, SignalTracker& other) {
    auto otherSignal { other.mSignals.at(theirSignalsName).lock() };
    assert(otherSignal && "No signal of this name found on other");

    auto ourObserver { mObservers.at(ourObserversName).lock() };
    assert(ourObserver && "No observer of this name present on this object");

    otherSignal->registerObserver(ourObserver);

    garbageCollection();
}
