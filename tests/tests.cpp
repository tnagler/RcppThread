// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppThread)]]

// https://github.com/RcppCore/Rcpp/commit/16848780ee764a83c00017c8c6e403b2192ea980
#ifdef __MACH__
#include <mach/boolean.h>
#endif
#include <Rcpp.h>

#include "RcppThread.h"
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
    ThreadPool pool(2);
    std::vector<size_t> x(1000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };
    for (size_t i = 0; i < x.size() / 2; i++)
        pool.push(dummy, i);

    std::cout << "CALLING WAIT" << std::endl;
    pool.wait();

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("push gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolPushReturn()
{
    ThreadPool pool(2);
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i)  {
        checkUserInterrupt();
        return 2 * x[i];
    };

    std::vector<std::future<size_t>> fut(x.size());
    for (size_t i = 0; i < x.size() / 2; i++)
        fut[i] = pool.pushReturn(dummy, i);
    for (size_t i = 0; i < x.size() / 2; i++)
        x[i] = fut[i].get();
    pool.join();

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("push gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolMap()
{
    ThreadPool pool(2);

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
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("map gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolParallelFor()
{
    ThreadPool pool(2);

    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    pool.parallelFor(0, x.size() / 2, dummy, 1);
    pool.join();

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolNestedParallelFor()
{
    ThreadPool pool(2);
    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    pool.parallelFor(0, x.size(), [&] (int i) {
        pool.parallelFor(0, x[i].size(), [&x, i] (int j) {
            x[i][j] *= 2;
        });
    });
    pool.wait();

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0) {
        for (auto xx : x) {
            for (auto xxx : xx)
                std::cout << xxx;
            std::cout << std::endl;
        }
        throw std::runtime_error("nested parallelFor gives wrong result");
    }
}

// [[Rcpp::export]]
void testThreadPoolParallelForEach()
{
    ThreadPool pool(2);

    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    auto ids = std::vector<size_t>(x.size() / 2);
    for (size_t i = 0; i < ids.size(); i++)
        ids[i] = i;
    pool.parallelForEach(ids, dummy);
    pool.join();

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelForEach gives wrong result");
}

// [[Rcpp::export]]
void testThreadPoolNestedParallelForEach()
{
    ThreadPool pool(2);

    std::vector<std::vector<double>> x(100);
    for (auto &xx : x)
        xx = std::vector<double>(100, 1.0);
    pool.parallelForEach(x, [&pool] (std::vector<double>& xx) {
        pool.parallelForEach(xx, [] (double& xxx) {
            xxx *= 2;
        });
    });
    pool.wait();

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0) {
        for (auto xx : x) {
            for (auto xxx : xx)
                std::cout << xxx;
            std::cout << std::endl;
        }
        throw std::runtime_error("nested parallelFor gives wrong result");
    }
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

    for (size_t i = 0; i < x.size() / 2; i++)
        pool.push(dummy, i);
    pool.wait();

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 2);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("push gives wrong result");
    pool.join();
}

// [[Rcpp::export]]
void testThreadPoolDestructWOJoin()
{
    ThreadPool pool(2);
}


// [[Rcpp::export]]
void testParallelFor()
{
    std::vector<size_t> x(1000000, 1);
    auto dummy = [&] (size_t i) -> void {
        checkUserInterrupt();
        x[i] = 2 * x[i];
    };

    parallelFor(0, x.size() / 2, dummy, 2);
    parallelFor(0, x.size() / 2, dummy, 0);

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 4);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("parallelFor gives wrong result");
}

// [[Rcpp::export]]
void testNestedParallelFor()
{
    std::vector<std::vector<double>> x(1);
    for (auto &xx : x)
        xx = std::vector<double>(1, 1.0);

    parallelFor(0, x.size(), [&x] (int i) {
        parallelFor(0, x[i].size(), [&x, i] (int j) {
            x[i][j] *= 2;
        }, 1);
    }, 1);

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0) {
        for (auto xx : x) {
            for (auto xxx : xx)
                std::cout << xxx;
            std::cout << std::endl;
        }
        throw std::runtime_error("nested parallelFor gives wrong result");
    }
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
    parallelForEach(ids, dummy, 2);
    parallelForEach(ids, dummy, 0);

    size_t count_wrong = 0;
    for (size_t i = 0; i < x.size() / 2; i++)
        count_wrong += (x[i] != 4);
    for (size_t i = x.size() / 2 + 1; i < x.size(); i++)
        count_wrong += (x[i] != 1);
    if (count_wrong > 0)
        throw std::runtime_error("forEach gives wrong result");
}

// [[Rcpp::export]]
void testNestedParallelForEach()
{
    std::vector<std::vector<double>> x(1);
    for (auto &xx : x)
        xx = std::vector<double>(1, 1.0);

    parallelForEach(x, [&] (std::vector<double>& xx) {
        parallelForEach(xx, [&] (double& xxx) {
            xxx *= 2;
        }, 1);
    }, 1);

    size_t count_wrong = 0;
    for (auto xx : x) {
        for (auto xxx : xx)
            count_wrong += xxx != 2;
    }
    if (count_wrong > 0)
        throw std::runtime_error("nested parallelForEach gives wrong result");

}

// // [[Rcpp::export]]
// void testThreadInterrupt()
// {
//     auto dummy = [] {
//         std::this_thread::sleep_for(std::chrono::milliseconds(500));
//         checkUserInterrupt();
//     };
//     Thread t(dummy);
//     t.join();
//     std::this_thread::sleep_for(std::chrono::milliseconds(5000));
// }
//
// // [[Rcpp::export]]
// void testThreadPoolInterruptJoin()
// {
//     ThreadPool pool(2);
//     auto dummy = [] {
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         checkUserInterrupt();
//     };
//     for (size_t i = 0; i < 20; i++)
//         pool.push(dummy);
//     pool.join();
//     std::this_thread::sleep_for(std::chrono::milliseconds(5000));
// }
//
// // [[Rcpp::export]]
// void testThreadPoolInterruptWait()
// {
//     ThreadPool pool(2);
//     auto dummy = [] {
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         checkUserInterrupt();
//     };
//     for (size_t i = 0; i < 20; i++) {
//         pool.push(dummy);
//     }
//     pool.wait();
//     std::this_thread::sleep_for(std::chrono::milliseconds(3000));
// }


// [[Rcpp::export]]
void testProgressCounter()
{
    RcppThread::ProgressCounter cntr(20, 1);
    RcppThread::parallelFor(0, 20, [&] (int i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cntr++;
    });
}

// [[Rcpp::export]]
void testProgressBar()
{
    // 20 iterations in loop, update progress every 1 sec
    RcppThread::ProgressBar bar(20, 1);
    RcppThread::parallelFor(0, 20, [&] (int i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++bar;
    });
}
