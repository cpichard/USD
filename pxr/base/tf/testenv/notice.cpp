//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/regTest.h"
#include "pxr/base/tf/notice.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/weakBase.h"
#include "pxr/base/tf/weakPtr.h"
#include "pxr/base/arch/systemInfo.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using std::cout;
using std::endl;
using std::ostream;
using std::string;
using std::stringstream;
using std::vector;

PXR_NAMESPACE_USING_DIRECTIVE

class TestNotice : public TfNotice {
public:
    TestNotice(const string& what)
	: _what(what) {
    }
    ~TestNotice();

    const string& GetWhat() const {
	return _what;
    }
    
private:
  const string _what;
};

TestNotice::~TestNotice() { }

class TestListener : public TfWeakBase {
public:
    explicit TestListener(int identity)
	: _identity(identity)
    {
    }

    //! Called when a notice of any type is sent
    void ProcessNotice(const TfNotice&) {
	printf("Listener #%d: ProcessNotice got a notice\n", _identity);
    }

    void ProcessTestNotice(const TestNotice& n) {
	printf("Listener #%d: ProcessTestNotice got %s\n", _identity,
	       n.GetWhat().c_str());
    }

    void ProcessMyTestNotice(const TestNotice& n,
                             TfWeakPtr<TestListener> const &sender) {
	if ( ! sender ) {
	    printf("Listener #%d: ProcessMyTestNotice got %s from unknown sender\n", _identity,
		   n.GetWhat().c_str());
	} else {
	    printf("Listener #%d: ProcessMyTestNotice got %s from Sender #%d\n", _identity,
		   n.GetWhat().c_str(), sender->_identity);
	}
    }

private:
    int _identity;
};


//=================================================================
// test of threaded notices

stringstream workerThreadLog;
stringstream mainThreadLog;
vector<string> workerThreadList;
vector<string> mainThreadList;
std::mutex workerThreadLock;
std::mutex mainThreadLock;

// Helper to lock and print the list of log strings alphabetically; then clear the list
static void 
_DumpLog(ostream *log, vector<string> *li, std::mutex *mutex) {
    std::lock_guard<std::mutex> lock(*mutex);
    std::sort(li->begin(), li->end());
    for(vector<string>::const_iterator n = li->begin(); 
        n != li->end(); ++ n) {
        *log << *n << endl;
    }
    li->clear();
}

class BaseNotice : public TfNotice {
public:
    BaseNotice(const string& what) : _what(what) {}
    ~BaseNotice();

    const string& GetWhat() const {
	return _what;
    }

protected:
    const string _what;
};

BaseNotice::~BaseNotice() { }

class MainNotice : public BaseNotice {
public:
    MainNotice(const string& what) : BaseNotice(what) {}
};

class WorkerNotice : public BaseNotice {
public:
    WorkerNotice(const string& what) : BaseNotice(what) {}
};

class MainListener : public TfWeakBase {
public:
    MainListener() {
	// Register for invokation in any thread
	TfWeakPtr<MainListener> me(this);
	TfNotice::Register(me, &MainListener::ProcessNotice);
	_processMainKey = 
            TfNotice::Register(me, &MainListener::ProcessMainNotice);
    }

    void Revoke() {
        TfNotice::Revoke(_processMainKey);
    }

    void ProcessNotice(const TfNotice &n) {
        std::lock_guard<std::mutex> lock(mainThreadLock);
        mainThreadList.push_back("MainListener::ProcessNotice got notice of"
                                 " type " + TfType::Find(n).GetTypeName());
    }

    void ProcessMainNotice(const MainNotice &n) {
        std::lock_guard<std::mutex> lock(mainThreadLock);
        mainThreadList.push_back("MainListener::ProcessMainNotice got " +
                                 n.GetWhat());
    }

private:
    TfNotice::Key _processMainKey;
};

class WorkListener : public TfWeakBase {
public:
    WorkListener() {
	// Register for exclusive invokation in the worker (current) thread
	TfWeakPtr<WorkListener> me(this);
	_key = TfNotice::Register(me, &WorkListener::ProcessWorkerNotice);
    }

    void Revoke() {
        TfNotice::Revoke(_key);
    }
    
    void ProcessWorkerNotice(const WorkerNotice &n) {
        std::lock_guard<std::mutex> lock(workerThreadLock);
	workerThreadList.push_back("WorkListener::ProcessWorkerNotice got " +
                                   n.GetWhat());
    }

private:
    TfNotice::Key _key;
};

void WorkTask() {
    // Create a listener for exclusive execution in the worker thread
    WorkListener *workListener = new WorkListener();

    // Send some notifications
    workerThreadLog << "// WorkListener should respond once\n";
    WorkerNotice("WorkerNotice 1").Send();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    _DumpLog(&workerThreadLog, &workerThreadList, &workerThreadLock);

    workListener->Revoke();

    workerThreadLog << "// WorkListener should not respond\n";
    WorkerNotice("WorkerNotice 2").Send();

    _DumpLog(&workerThreadLog, &workerThreadList, &workerThreadLock);

    delete workListener;
}
	

static bool
_TestThreadedNotices()
{
    // Create and register the main listener
    MainListener *mainListener = new MainListener();

    mainThreadLog << "// MainListener should respond four times\n";

    // start the worker thread
    std::thread workerThread(WorkTask);

    MainNotice("Main notice 1").Send();
    
    workerThread.join();
    
    mainListener->Revoke();
    
    _DumpLog(&mainThreadLog, &mainThreadList, &mainThreadLock);
    
    mainThreadLog << "// MainListener::ProcessNotice should respond once\n";
    MainNotice("Main notice 2").Send();

    _DumpLog(&mainThreadLog, &mainThreadList, &mainThreadLock);
    
    delete mainListener;

    mainThreadLog << "// MainListener should not respond\n";
    MainNotice("main: Error!").Send();

    _DumpLog(&mainThreadLog, &mainThreadList, &mainThreadLock);

    cout << "\n--- Main Thread Log ---\n";
    cout << mainThreadLog.str();

    cout << "\n--- Work Thread Log ---\n";
    cout << workerThreadLog.str();
    
    return true;
}

struct SpoofSender : public TfWeakBase {
};

struct SpoofCheckListener : public TfWeakBase {
    void ListenA(const TfNotice&, TfWeakPtr<SpoofSender> const &) {
	printf("SpoofCheckListener: A\n");
	hits++;
    }

    void ListenB(const TfNotice&) {
	printf("SpoofCheckListener: B\n");
	hits++;
    }

    void ListenC(const TfNotice&, TfType const &,
                 TfWeakBase*, const void *, const std::type_info&) {
	printf("SpoofCheckListener: C\n");
	hits++;
    }

    SpoofCheckListener() {
	hits = 0;
    }
    
    int hits;
};

static void
_TestSpoofedNotices()
{
    SpoofCheckListener listener;
    char rawSpace[1024];

    SpoofSender* rawSender = new (rawSpace) SpoofSender;
    TfWeakPtr<SpoofSender> sender = TfCreateWeakPtr(rawSender);

    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenA, sender);
    
    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenB, sender);

    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenC,
                       TfType::Find<TfNotice>(),
		       TfAnyWeakPtr(sender));

    TF_AXIOM(listener.hits == 0);
    printf("Expecting no replies to send...\n");
    TfNotice().Send();
    TF_AXIOM(listener.hits == 0);
    printf("Expecting 3 replies to send...\n");
    TfNotice().Send(sender);
    TF_AXIOM(listener.hits == 3);

    listener.hits = 0;
    
    sender->~SpoofSender();
    
    TF_AXIOM(sender.IsInvalid());
    
    SpoofSender* rawSender2 = new (rawSpace) SpoofSender;
    TfWeakPtr<SpoofSender> sender2 = TfCreateWeakPtr(rawSender2);

    printf("Expecting no replies to send...\n");
    TfNotice().Send(sender2);
    TF_AXIOM(listener.hits == 0);

    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenA, TfWeakPtr<SpoofSender>(NULL));
    
    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenB);

    TfNotice::Register(TfCreateWeakPtr(&listener),
		       &SpoofCheckListener::ListenC,
                       TfType::Find<TfNotice>(),
		       TfAnyWeakPtr());

    printf("Expecting 3 replies to send...\n");
    TfNotice().Send(sender2);
    TF_AXIOM(listener.hits == 3);

    listener.hits = 0;
    
    printf("Expecting 3 replies to send...\n");
    TfNotice().Send();
    TF_AXIOM(listener.hits == 3);
}

struct BlockListener : public TfWeakBase
{
    BlockListener() : mainId(std::this_thread::get_id())
    {
        hits[0] = 0;
        hits[1] = 0;
        TfNotice::Register(TfCreateWeakPtr(this), &BlockListener::Listen);
    }

    void Listen(const TfNotice& n)
    {
        ++hits[std::this_thread::get_id() == mainId ? 0 : 1];
    }

    const std::thread::id mainId;
    size_t hits[2];
};

static void
_TestNoticeBlockWorker(std::thread::id mainId)
{
    struct _Work {
        static void Go()
        {
            for (int i = 0; i < 20; ++i) {
                TestNotice(TfStringPrintf("Notice %d", i)).Send();
            }
        }
    };

    if (std::this_thread::get_id() == mainId) {
        TfNotice::Block block;
        _Work::Go();
    }
    else {
        _Work::Go();
    }
}

static void
_TestNoticeBlock()
{
    BlockListener l;
    TestNotice("should not be blocked").Send();
    TF_AXIOM(l.hits[0] == 1);
    TF_AXIOM(l.hits[1] == 0);

    {
        TfNotice::Block noticeBlock;
        TestNotice("should be blocked").Send();
        TF_AXIOM(l.hits[0] == 1);
        TF_AXIOM(l.hits[1] == 0);

        TestNotice("should be blocked too").Send();
        TF_AXIOM(l.hits[0] == 1);
        TF_AXIOM(l.hits[1] == 0);
    }

    TestNotice("should not be blocked").Send();
    TF_AXIOM(l.hits[0] == 2);
    TF_AXIOM(l.hits[1] == 0);

    std::thread t(_TestNoticeBlockWorker, std::this_thread::get_id());
    _TestNoticeBlockWorker(std::this_thread::get_id());
    t.join();

    TF_AXIOM(l.hits[0] == 2);
    TF_AXIOM(l.hits[1] == 20);
}

class TestRevokeAndWaitListener : public TfWeakBase {
public:
    TestRevokeAndWaitListener()
    {
        _key = TfNotice::Register(TfCreateWeakPtr(this),
                                  &TestRevokeAndWaitListener::_Handler);
    }

    ~TestRevokeAndWaitListener()
    {
        // Wait for the handler to be revoked.
        TfNotice::RevokeAndWait(_key);

        // Cause _Handler() to assert if it gets called.
        _alive = false;

        // Let in-flight sends call _Handler if they're going to.  This
        // makes potential race conditions more likely to trigger the
        // assert.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

private:
    void _Handler(const TestNotice&)
    {
        // If this assert fails then the most likely cause is that
        // TfNotice::_DelivererBase::_WaitForSendsToFinish() doesn't
        // wait for all sends to complete before returning.  It must
        // not return until _SendToListenerImpl() for the listener
        // is not being called and cannot later be called.
        TF_AXIOM(_alive);
    }

private:
    bool _alive{true};
    TfNotice::Key _key;
};

static void
_TestRevokeAndWait()
{
    // Test that RevokeAndWait() really waits like it's supposed to.  This
    // is a stress test to check for race conditions.  We start a bunch of
    // threads and register/send/revoke in each at different cadences.

    // The number of threads to use.
    static constexpr int numThreads = 20;

    // The number of total sends across all threads.  The execution time of
    // the test is directly proportional to this.
    static constexpr int numSends = 60000;

    // The number of sends remaining.  Threads wait to start until this is
    // no longer zero.
    std::atomic<int> sendsRemaining{0};

    // Threads use this to get a unique id in the range [1,numThreads].
    // We also use it to wait for all threads to have started.
    std::atomic<int> id{0};

    // The work for each thread.
    auto task =
        [&]()
        {
            // Increase the number of sends per loop below by this much.
            // Each thread uses a different number to ensure they don't
            // somehow go in lock step.
            const int step = ++id;

            // We can send the same notice each time, avoiding the overhead
            // of making a string copy.
            TestNotice notice(TfStringPrintf("step %d", step));

            // Synchronize starting.
            while (sendsRemaining == 0) {
                std::this_thread::yield();
            }

            // Create a listener, send the notice a bunch of times, destroy
            // the listener, and repeat until we've done enough sends.  We
            // Increase the number of sends per loop just to mix things up.
            int n = 10;
            while (true) {
                TestRevokeAndWaitListener listener;
                for (int i = 0; i != n; ++i) {
                    if (--sendsRemaining <= 0) {
                        return;
                    }
                    notice.Send();
                }
                n += step;
            }
        };

    // Start the threads.
    std::vector<std::thread> threads(numThreads);
    for (auto& thread: threads) {
        thread = std::thread(task);
    }

    // Let the threads start up and synchronize then let them run.
    while (id != numThreads) {
        std::this_thread::yield();
    }
    sendsRemaining.store(numSends);

    // Wait for threads to finish.
    for (auto& thread: threads) {
        thread.join();
    }
}

static bool
Test_TfNotice()
{

    TestListener *l1 = new TestListener(1);
    TestListener *l2 = new TestListener(2);
    TfWeakPtr<TestListener> wl1(l1),
			    wl2(l2);
    TfNotice::Key l1Key1 = TfNotice::Register(wl1, &TestListener::ProcessNotice);
    /*TfNotice::Key l1Key2 =*/ TfNotice::Register(wl1, &TestListener::ProcessTestNotice);
    /*TfNotice::Key l2Key1 =*/ TfNotice::Register(wl2, &TestListener::ProcessNotice);
    TfNotice::Key l2Key2 = TfNotice::Register(wl2, &TestListener::ProcessTestNotice);

    TfNotice::Key l2Key4 = TfNotice::Register(wl2, &TestListener::ProcessMyTestNotice, wl2);
    
    printf("// Expect: #1 ProcessNotice\n");
    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from unknown\n");
    TestNotice("first").Send();
    
    printf("// Expect: #1 ProcessNotice\n");
    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    TestNotice("second").Send(wl2);
    
    printf("// Expect: #1 ProcessNotice\n");
    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from #1\n");
    TfNotice::Revoke(l2Key2);
    TestNotice("third").Send(wl1);

    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    TfNotice::Revoke(l1Key1);
    TestNotice("fourth").Send(wl2);


    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    TfNotice::Revoke(l2Key4);
    TestNotice("fifth").Send(wl2);

    printf("// Expect: #1 ProcessTestNotice\n");
    printf("// Expect: #2 ProcessNotice\n");
    printf("// Expect: #2 ProcessMyTestNotice from #2\n");
    TestNotice("sixth").SendWithWeakBase(&wl2->__GetTfWeakBase__(),
                                         wl2.GetUniqueIdentifier(),
                                         typeid(TestListener));

    delete l2;

    printf("// Expect: #1 ProcessTestNotice\n");
    TestNotice("seventh").Send(wl2);

    delete l1;

    printf("// Expect: nothing\n");
    TestNotice("error!").Send();

    _TestThreadedNotices();

    _TestSpoofedNotices();

    _TestNoticeBlock();

    _TestRevokeAndWait();
    
    return true;
}

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<TestNotice, TfType::Bases<TfNotice> >();
    TfType::Define<MainNotice, TfType::Bases<TfNotice> >();
    TfType::Define<WorkerNotice, TfType::Bases<TfNotice> >();
}

TF_ADD_REGTEST(TfNotice);
