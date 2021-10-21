#
# Rcpp::sourceCpp(code = {
#     '
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::depends(RcppThread)]]
#
# #include "RcppThread.h"
#
# // [[Rcpp::export]]
# void testTaskQueue()
# {
#     RcppThread::TaskQueue queue(128);
#     std::thread thread1([&] {
#         for (int i = 0; i < 400; i++)
#             queue.push([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
#     });
#
#     std::thread thread2([&] {
#         for (int i = 0; i < 100; i++)
#             queue.pop()();
#     });
#     std::thread thread3([&] {
#         for (int i = 0; i < 100; i++)
#             queue.pop()();
#     });
#     std::thread thread4([&] {
#         for (int i = 0; i < 100; i++)
#             queue.pop()();
#     });
#     thread1.join();
#     thread2.join();
#     thread3.join();
#     thread4.join();
# }
# '
# })
#
# testTaskQueue()
#
#
# Rcpp::sourceCpp(code = {
# '
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::depends(RcppThread)]]
#
# #include "RcppThread.h"
#
# // [[Rcpp::export]]
# void testTaskQueueFromPool()
# {
#     std::vector<int> x(2056);
#     RcppThread::ThreadPool pool(10);
#     for (int i = 0; i < 2056; i++)
#         pool.push([&, i] { x[i] = 1; });
#     RcppThread::Rcout << "waiting" << std::endl;
#     pool.wait();
#     RcppThread::Rcout << "done waiting" << std::endl;
#
#     for (auto xx : x) {
#         if (xx != 1)
#              throw std::runtime_error("pool did not execute task");
#     }
#     RcppThread::Rcout << "passed checks" << std::endl;
#     std::this_thread::sleep_for(std::chrono::milliseconds(200));
# }
# '
# })
#
# testTaskQueueFromPool()

