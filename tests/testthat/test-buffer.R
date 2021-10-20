
Rcpp::sourceCpp(code = {
    '
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppThread)]]

#include "RcppThread.h"

// [[Rcpp::export]]
void testTaskQueue()
{
    RcppThread::detail::RingBuffer buff(2);

    RcppThread::TaskQueue queue(128);
    queue.push(std::function<void()>());
    queue.push(std::function<void()>());
    queue.pop();
    queue.pop();
    queue.pop();
    queue.pop();

    std::thread thread1([&] {
        for (int i = 0; i < 400; i++)
            queue.push([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    });

    std::thread thread2([&] {
        for (int i = 0; i < 100; i++)
            queue.pop()();
    });
    std::thread thread3([&] {
        for (int i = 0; i < 100; i++)
            queue.pop()();
    });
    std::thread thread4([&] {
        for (int i = 0; i < 100; i++)
            queue.pop()();
    });
    thread1.join();
    thread2.join();
    thread3.join();
    thread4.join();
}
'
})

testTaskQueue()


Rcpp::sourceCpp(code = {
'
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppThread)]]

#include "RcppThread.h"

// [[Rcpp::export]]
void testTaskQueueFromPool()
{
    RcppThread::detail::RingBuffer buff(2);

    RcppThread::TaskQueue queue(128);
    queue.push(std::function<void()>());
    queue.push(std::function<void()>());
    queue.pop();
    queue.pop();
    queue.pop();
    queue.pop();

    std::thread thread1([&] {
        for (int i = 0; i < 400; i++)
            queue.push([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    });

    RcppThread::ThreadPool pool(4);
    for (int i = 0; i < 400; i++)
        pool.push([&queue] { queue.pop()(); });
    thread1.join();
    pool.join();
}
'
})

testTaskQueueFromPool()

