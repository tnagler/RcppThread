#include <Rcpp.h>

#include "RcppThreads/RMonitor.hpp"
#include "RcppThreads/Thread.hpp"

using namespace RcppThreads;

// [[Rcpp::export]]
void testMonitor()
{
    auto checks = [] () -> void {
        checkUserInterrupt();           // should have no effect since not master
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
