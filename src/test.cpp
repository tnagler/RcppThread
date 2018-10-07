// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

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
void testThreadPoolPush()
{
    ThreadPool pool;
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };
    for (int i = 0; i < x.size() / 2; i++)
        pool.push(dummy, i);

    pool.join();
    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("push gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolMap()
{
    ThreadPool pool;

    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    auto ids = std::vector<size_t>(x.size() / 2);
    for (size_t i = 0; i < ids.size(); i++)
        ids[i] = i;
    pool.map(dummy, ids);
    pool.join();

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("map gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolParallelFor()
{
    ThreadPool pool;

    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    pool.parallelFor(0, x.size() / 2, dummy, 1);
    pool.join();

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolForEach()
{
    ThreadPool pool;

    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    auto ids = std::vector<size_t>(x.size() / 2);
    for (size_t i = 0; i < ids.size(); i++)
        ids[i] = i;
    pool.forEach(ids, dummy);
    pool.join();

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("forEach gives wrong result");
}


// [[Rcpp::export]]
void testThreadPoolSingleThreaded()
{
    ThreadPool pool;
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    for (int i = 0; i < x.size() / 2; i++)
        pool.push(dummy, i);
    pool.wait();

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("push gives wrong result");
}


// [[Rcpp::export]]
void testParallelFor()
{
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    parallelFor(0, x.size() / 2, dummy);
    parallelFor(0, x.size() / 2, dummy, 0);

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 4);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testForEach()
{
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    auto ids = std::vector<size_t>(x.size() / 2);
    for (size_t i = 0; i < ids.size(); i++)
        ids[i] = i;
    parallelForEach(ids, dummy);
    parallelForEach(ids, dummy, 0);

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 4);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("forEach gives wrong result");
}

// [[Rcpp::export]]
void testThreadInterrupt()
{
    auto dummy = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        checkUserInterrupt();
    };
    Thread t(dummy);
    t.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
}

// [[Rcpp::export]]
void testPoolInterruptJoin()
{
    ThreadPool pool;
    auto dummy = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        checkUserInterrupt();
    };
    for (size_t i = 0; i < 10; i++)
        pool.push(dummy);
    pool.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
}

// [[Rcpp::export]]
void testPoolInterruptWait()
{
    ThreadPool pool;
    auto dummy = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        checkUserInterrupt();
    };
    for (size_t i = 0; i < 10; i++)
        pool.push(dummy);
    pool.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    pool.join();
}
