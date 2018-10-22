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
        checkUserInterrupt();  // should have no effect since not main
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
void testThreadPoolNestedParallelFor()
{
    ThreadPool pool;
    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    pool.parallelFor(0, x.size(), [&] (int i) {
        pool.parallelFor(0, x[i].size(), [&x, i] (int j) {
            x[i][j] *= 2;
        });
    });
    pool.join();

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0)
        throw std::runtime_error("nested parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolParallelForEach()
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
    pool.parallelForEach(ids, dummy);
    pool.wait();

    size_t count_wrong = 0;
    for (int i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (int i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelForEach gives wrong result");
    pool.join();
}

// [[Rcpp::export]]
void testThreadPoolNestedParallelForEach()
{
    ThreadPool pool;

    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    pool.parallelForEach(x, [&pool] (std::vector<double>& xx) {
        pool.parallelForEach(xx, [] (double& xxx) {
            xxx *= 2;
        });
    });
    pool.join();

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0)
        throw std::runtime_error("nested parallelForEach gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolSingleThreaded()
{
    ThreadPool pool(0);
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
    pool.join();
}

// [[Rcpp::export]]
void testThreadPoolDestructWOJoin()
{
    ThreadPool pool;
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
void testNestedParallelFor()
{
    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    parallelFor(0, x.size(), [&x] (int i) {
        parallelFor(0, x[i].size(), [&x, i] (int j) {
            x[i][j] *= 2;
        });
    });

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0)
        throw std::runtime_error("nested parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testParallelForEach()
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
void testNestedParallelForEach()
{
    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    parallelForEach(x, [] (std::vector<double>& xx) {
        parallelForEach(xx, [] (double& xxx) {
            xxx *= 2;
        });
    });

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0)
        throw std::runtime_error("nested parallelForEach gives wrong result");

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
void testThreadPoolInterruptJoin()
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
void testThreadPoolInterruptWait()
{
    ThreadPool pool(0);
    auto dummy = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        checkUserInterrupt();
    };
    for (size_t i = 0; i < 20; i++) {
        pool.push(dummy);
    }
    pool.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
}

// [[Rcpp::export]]
void test()
{
    ThreadPool pool;
    std::vector<std::vector<double>> x(3);
    for (auto &xx : x)
        xx = std::vector<double>(3, 1.0);
    pool.parallelFor(0, x.size(), [&] (int j) {
        auto temp = x[j];
        pool.parallelFor(0, temp.size(), [&temp] (int i) {
            temp[i] *= 2;
        });
        x[j] = temp;
        // xvec = std::vector<double>(10, 2.0);
    });
    pool.wait();
    pool.join();
    for (auto xvec : x) {
        for (auto xx : xvec)
            std::cout << xx << std::endl;
        std::cout << std::endl;
    }
}
