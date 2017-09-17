#include <Rcpp.h>

#include "RcppThreads/RMonitor.hpp"
#include "RcppThreads/Thread.hpp"
#include "RcppThreads/ThreadPool.hpp"

using namespace RcppThreads;

// [[Rcpp::export]]
void testMonitor()
{
    auto checks = [] () -> void {
        checkInterrupt();               // should have no effect since not master
        print("RcppThreads says hi!");  // should print to R console
        if (isInterrupted())
            throw std::runtime_error("isInterrupted should not return 'true'");
        if (isInterrupted(false))
            throw std::runtime_error("isInterrupted checks despite condition is 'false'");
    };

    std::thread t = std::thread(checks);
    t.join();
    releaseMsgBuffer();
}

// [[Rcpp::export]]
void testThreadClass()
{
    //  check if all methods work
    int printID = 1;
    auto dummy = [&] () -> void {
        print(printID++);
    };
    Thread(dummy).join();
    Thread(dummy).detach();
    Thread t1 = Thread(dummy);
    Thread t2 = Thread(dummy);
    t1.swap(t2);
    t1.joinable();
    t1.join();
    t2.join();
}

// [[Rcpp::export]]
void testThreadPool()
{
    std::atomic_int printID;
    printID.store(1);
    auto dummy = [&] () -> void {
        print(printID++);
    };
    ThreadPool pool(3);
    for (int i = 0; i < 50; i++)
        pool.push(dummy);
    pool.wait();
}
