// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThreads and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThreads or https://github.com/tnagler/RcppThreads/blob/master/LICENSE.md.

#include <Rcpp.h>

#include "RcppThreads.h"

using namespace RcppThreads;

// [[Rcpp::export]]
void testMonitor()
{
    auto checks = [] () -> void {
        checkUserInterrupt();               // should have no effect since not master
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
        std::this_thread::sleep_for(std::chrono::seconds(1));
    };
    Thread(dummy).join();
    Thread t0(dummy);
    t0.detach();
    if (t0.joinable())
        throw std::runtime_error("thread wasn't detached");
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
