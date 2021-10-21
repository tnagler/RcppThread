Rcpp::sourceCpp(code = {
'
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppThread)]]

#include "RcppThread.h"

// [[Rcpp::export]]
void testTaskQueueFromPool()
{
    std::vector<int> x(2056);
    RcppThread::ThreadPool pool(10);
    for (int i = 0; i < 2056; i++)
        pool.push([&, i] { x[i] = 1;     std::this_thread::sleep_for(std::chrono::milliseconds(200));
 });
    RcppThread::Rcout << "waiting" << std::endl;
    pool.wait();
    RcppThread::Rcout << "done waiting" << std::endl;

    for (auto xx : x) {
        if (xx != 1)
             throw std::runtime_error("pool did not execute task");
    }
    RcppThread::Rcout << "passed checks" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
'
})

testTaskQueueFromPool()

