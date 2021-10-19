test <- Rcpp::cppFunction( code = {
    "
    void run() {
        RcppThread::ProgressCounter cnt(20, 1);
        RcppThread::parallelFor(0, 20, [&] (int i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            cnt++;
        });

        RcppThread::ProgressBar cnt2(20, 1);
        RcppThread::parallelFor(0, 20, [&] (int i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            cnt2++;
        });
  }
    "
}, depends = "RcppThread", plugins = "cpp11")

test()

