#include <Rcpp.h>

#include "RcppThreads/RMonitor.hpp"


// [[Rcpp::export]]

void testMonitor()
{
    auto checks = [] () {
        RcppThreads::checkUserInterrupt();
        RcppThreads::print("RcppThreads says hi!");
        if (RcppThreads::isInterrupted())
            throw std::runtime_error("isInterrupted should not return 'true'");
        if (RcppThreads::isInterrupted(false))
            throw std::runtime_error("isInterrupted checks despite condition is 'false'");
    };

    std::thread t = std::thread(checks);
    t.join();
    RcppThreads::releaseMsgBuffer();
}
