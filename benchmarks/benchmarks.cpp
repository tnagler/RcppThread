// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::plugins(openmp)]]
// [[Rcpp::depends(RcppThread)]]
// [[Rcpp::depends(RcppParallel)]]
// [[Rcpp::depends(RcppEigen)]]
// [[Rcpp::depends(wdm)]]

#include <Eigen/Dense>
#include <Rcpp.h>
#include <RcppParallel.h>
#include <RcppThread.h>
#include <omp.h>
#include <wdm/eigen.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <vector>

namespace bench {

double
time_one(const std::function<void()>& f)
{
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void
time_once(const std::vector<std::function<void()>>& funcs,
          std::vector<double>& times)
{
    for (size_t k = 0; k < funcs.size(); k++) {
        times[k] += time_one(funcs[k]);
    }
}

std::vector<double>
mark(std::vector<std::function<void()>> funcs, double min_sec = 1)
{
    size_t min_time = 0;
    std::vector<double> times(funcs.size(), 0);
    while (min_time < min_sec) {
        time_once(funcs, times);
        min_time = *std::max_element(times.begin(), times.end());
    }
    return times;
}

} // end namespace bench

class BenchMethods
{
    size_t n_;
    std::function<void(int)> func_;

  public:
    BenchMethods(size_t n, std::function<void(int)> func)
      : n_{ n }
      , func_(func)
    {}

    void singleThreaded(int n)
    {
        for (size_t i = 0; i < n; ++i)
            func_(i);
    }

    void ThreadPool(int n)
    {
        for (size_t i = 0; i < n; ++i)
            RcppThread::push(func_, i);
        RcppThread::wait();
    }

    void parallelFor(int n)
    {
        RcppThread::parallelFor(0, n, func_);
    }

    void OpenMP_static(int n)
    {
#pragma omp parallel for schedule(static)
        for (size_t i = 0; i < n; ++i)
            func_(i);
    }

    void OpenMP_dynamic(int n)
    {
#pragma omp parallel for schedule(dynamic)
      for (size_t i = 0; i < n; ++i)
        func_(i);
    }

    void RcppParallelFor(int n)
    {
        struct Job : public RcppParallel::Worker
        {
            std::function<void(int)> f;
            void operator()(std::size_t begin, std::size_t end)
            {
                for (size_t i = begin; i < end; i++)
                    f(i);
            }
        } job;
        job.f = func_;
        RcppParallel::parallelFor(0, n, job);
    }
};

Rcpp::NumericVector
benchMark(std::function<void(int i)> task, size_t n, double min_sec = 10)
{
    BenchMethods methods(n, task);
    auto times = bench::mark({
                               [&] { methods.singleThreaded(n); },
                               [&] { methods.ThreadPool(n); },
                               [&] { methods.parallelFor(n); },
                               [&] { methods.OpenMP_static(n); },
                               [&] { methods.OpenMP_dynamic(n); },
                               [&] { methods.RcppParallelFor(n); },
                             },
                             min_sec);

    // compute speed up over single threaded
    const auto t0 = times[0];
    for (auto& t : times)
        t = t0 / t;

    return Rcpp::wrap(times);
}

// [[Rcpp::export]]
Rcpp::NumericMatrix
benchEmpty(Rcpp::IntegerVector ns, double min_sec = 10)
{
    Rcpp::NumericMatrix times(ns.size(), 6);
    for (int i = 0; i < ns.size(); i++) {
        times(i, Rcpp::_) = benchMark([](int i) {}, ns[i], min_sec);
    }

    colnames(times) = Rcpp::CharacterVector{
        "single", "quickpool::push", "quickpool::parallel_for",
        "OpenMP static", "OpenMP dynamic",
        "Intel TBB"
    };

    return times;
}

// [[Rcpp::export]]
Rcpp::NumericMatrix
benchSqrt(Rcpp::IntegerVector ns, double min_sec = 10)
{
    auto op = [](double x) {
        double xx = x;
        for (int j = 0; j != 1000; j++) {
            xx = std::sqrt(xx);
        }
    };
    Rcpp::NumericMatrix times(ns.size(), 6);
    for (int i = 0; i < ns.size(); i++) {
        std::vector<double> x(ns[i], 3.14);
        times(i, Rcpp::_) = benchMark([&](int i) { op(x[i]); }, ns[i], min_sec);
    }

    colnames(times) = Rcpp::CharacterVector{
      "single", "quickpool::push", "quickpool::parallel_for",
      "OpenMP static", "OpenMP dynamic",
      "Intel TBB"
    };

    return times;
}

// [[Rcpp::export]]
Rcpp::NumericMatrix
benchSqrtWrite(std::vector<int> ns, double min_sec = 10)
{
    auto op = [](double& x) {
        for (int j = 0; j != 1000; j++) {
            x = std::sqrt(x);
        }
    };
    Rcpp::NumericMatrix times(ns.size(), 6);
    for (int i = 0; i < ns.size(); i++) {
        std::vector<double> x(ns[i], 3.14);
        times(i, Rcpp::_) = benchMark([&](int i) { op(x[i]); }, ns[i], min_sec);
    }

    colnames(times) = Rcpp::CharacterVector{
      "single", "quickpool::push", "quickpool::parallel_for",
      "OpenMP static", "OpenMP dynamic",
      "Intel TBB"
    };

    return times;
}

// [[Rcpp::export]]
Rcpp::NumericMatrix
benchKDE(std::vector<int> ns, size_t d, double min_sec = 10)
{
    using namespace Eigen;
    auto kernel = [](const VectorXd& x) {
        return (-x.array().pow(2) / 2).exp() / std::sqrt(3.14159 * 2);
    };
    auto kde = [=](const VectorXd& x) {
        double n = x.size();
        double sd = std::sqrt((x.array() - x.mean()).square().sum() / (n - 1));
        double bw = 1.06 * sd * std::pow(n, -0.2);
        VectorXd grid = VectorXd::LinSpaced(500, -1, 1);
        VectorXd fhat(grid.size());
        for (size_t i = 0; i < grid.size(); ++i) {
            fhat(i) = kernel((x.array() - grid(i)) / bw).mean() / bw;
        }
        return fhat;
    };

    Rcpp::NumericMatrix times(ns.size(), 6);
    for (int i = 0; i < ns.size(); i++) {
        MatrixXd x = MatrixXd(ns[i], d).setRandom();
        times(i, Rcpp::_) =
          benchMark([&](int i) { kde(x.col(i)); }, d, min_sec);
    }

    colnames(times) = Rcpp::CharacterVector{
      "single", "quickpool::push", "quickpool::parallel_for",
      "OpenMP static", "OpenMP dynamic",
      "Intel TBB"
    };

    return times;
}

// [[Rcpp::export]]
Rcpp::NumericMatrix
benchKendall(std::vector<int> ns, size_t d, double min_sec = 10)
{
    using namespace Eigen;
    Rcpp::NumericMatrix times(ns.size(), 6);
    for (int i = 0; i < ns.size(); i++) {
        MatrixXd x = MatrixXd(ns[i], d).setRandom();
        MatrixXd res = MatrixXd::Identity(d, d);
        auto ktau = [&](size_t i, size_t j) {
            res(i, j) = wdm::wdm(x.col(i), x.col(j), "kendall");
            res(j, i) = res(i, j);
        };

        auto task = [&](int i) {
            for (size_t j = i + 1; j < d; ++j) {
                ktau(i, j);
            }
        };

        times(i, Rcpp::_) = benchMark(task, ns[i], min_sec);
    }

    colnames(times) = Rcpp::CharacterVector{
      "single", "quickpool::push", "quickpool::parallel_for",
      "OpenMP static", "OpenMP dynamic",
      "Intel TBB"
    };

    return times;
}
