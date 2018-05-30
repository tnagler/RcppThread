// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#include <Rcpp.h>

#include "RcppThread.h"

#include <atomic>

using namespace RcppThread;

// [[Rcpp::export]]
void testMonitor()
{
    auto checks = [] () -> void {
        checkUserInterrupt();  // should have no effect since not master
        Rcout << "RcppThread says hi!" << std::endl; // should print to R console
        if (isInterrupted())
            throw std::runtime_error("isInterrupted should not return 'true'");
        if (isInterrupted(false))
            throw std::runtime_error("isInterrupted checks despite condition is 'false'");
    };

    std::thread t = std::thread(checks);
    t.join();
    Rcout << "";
}

// [[Rcpp::export]]
void testThreadClass()
{
    //  check if all methods work
    std::atomic<int> printID;
    printID = 1;
    auto dummy = [&] () -> void {
        checkUserInterrupt();
        Rcout << printID++;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    };
    Thread(dummy).join();
    Thread t0(dummy);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
    auto dummy = [&] (size_t id) -> void {
        checkUserInterrupt();
        Rcout << printID++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };
    ThreadPool pool(2);
    for (int i = 0; i < 50; i++)
        pool.push(dummy, i);
    std::vector<size_t> ids{1, 2, 3};
    pool.map(dummy, ids);
    pool.join();
}

// [[Rcpp::export]]
void testWait()
{
    std::atomic_bool finished;
    finished = false;
    std::vector<double> vec(10, 1.0);
    std::mutex m;
    auto dummy = [&] () -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        {
            std::unique_lock<std::mutex> lk(m);
            vec[0] = 0.0;
        }
        finished = true;
    };
    ThreadPool pool(3);
    for (int i = 0; i < 300; i++)
        pool.push(dummy);
    Rcout << "pushed" << std::endl;
    pool.wait();
    Rcout << "finished" << std::endl;
    pool.join();
}

// [[Rcpp::export]]
void testSingleThreaded()
{
    std::atomic_bool finished;
    finished = false;
    auto dummy = [&] () -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        finished = true;
    };
    ThreadPool pool(0);
    for (int i = 0; i < 3; i++)
        pool.push(dummy);
    Rcout << "pushed" << std::endl;
    pool.wait();
    Rcout << "finished" << std::endl;
    pool.join();
}
