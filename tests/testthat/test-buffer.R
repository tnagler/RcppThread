# Rcpp::sourceCpp(code = {
# '
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::depends(RcppThread)]]
#
# #include "RcppThread.h"
#
# // [[Rcpp::export]]
# void testThreadClass()
# {
#     RcppThread::detail::RingBuffer buff(2);
#     RcppThread::TaskQueue queue(4);
#     queue.push(std::function<void()>());
#     queue.push(std::function<void()>());
#     queue.pop();
#     queue.pop();
#     queue.pop();
#     queue.pop();
#     RcppThread::ThreadPool pool;
# }
# '
# })
