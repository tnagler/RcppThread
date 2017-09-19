#include <RcppThread.h>
#include <thread>  // C++11 threads

// [[Rcpp::export]]
void pyjamaParty()
{
    // some work that will be done in separate threads
    auto work = [] {
        auto id = std::this_thread::get_id();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        RcppThread::Rcout << id << " slept for one second" << std::endl;

        // Rcpp::checkUserInterrupt();      // R would crash
        RcppThread::checkUserInterrupt();  // does not crash

        std::this_thread::sleep_for(std::chrono::seconds(1));
        RcppThread::Rcout << id << " slept for another second" << std::endl;
    };

    // create two new threads
    std::thread t1(work);
    std::thread t2(work);

    // wait for threads to finish work
    t1.join();
    t2.join();
}

// [[Rcpp::export]]
void pyjamaParty2()
{
    // some work that will be done in separate threads
    auto work = [] {
        auto id = std::this_thread::get_id();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        RcppThread::Rcout << id << " slept for one second" << std::endl;

        RcppThread::checkUserInterrupt();

        std::this_thread::sleep_for(std::chrono::seconds(1));
        RcppThread::Rcout << id << " slept for another second" << std::endl;
    };

    // create two new threads
    RcppThread::Thread t1(work);
    RcppThread::Thread t2(work);

    // wait for threads to finish work
    t1.join();
    t2.join();
}

// [[Rcpp::export]]
void pool()
{
    // set up thread pool with 3 threads
    RcppThread::ThreadPool pool(3);

    // each tasks prints thread id
    auto task = [] () {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        RcppThread::checkUserInterrupt();
        RcppThread::Rcout <<
            "Task fetched by thread " << std::this_thread::get_id() << "\n";
    };

    // push 10 tasks to the pool
    for (int i = 0; i < 10; i++)
        pool.push(task);

    // wait for pool to finish work
    pool.join();
}
