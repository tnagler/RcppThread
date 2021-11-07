library("tidyverse")
library("microbenchmark")
library("Rcpp")
library("RcppParallel")
library("RcppThread")

# # Measuring synchronization overhead ------------------------
#
# ## Create, join, and destruct threads
#
# Rcpp::sourceCpp(code =
# "
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::depends(RcppThread)]]
# #include <RcppThread.h>
#
# // [[Rcpp::export]]
# void stdEmpty(int nThreads)
# {
#     std::vector<std::thread> threads;
#     for (size_t t = 0; t < nThreads; ++t) {
#         threads.emplace_back( [] {} );
#     }
#     for (size_t t = 0; t < nThreads; ++t) {
#         threads[t].join();
#     }
# }
#
# // [[Rcpp::export]]
# void RcppThreadEmpty(int nThreads)
# {
#     std::vector<RcppThread::Thread> threads;
#     for (size_t t = 0; t < nThreads; ++t) {
#         threads.emplace_back( [] {} );
#     }
#     for (size_t t = 0; t < nThreads; ++t) {
#         threads[t].join();
#     }
# }
# ")
# #
# timing <- tibble(
#     threads = c(1:4, seq.int(5, 50, l = 6)),
#     timings = map(
#         threads,
#         ~ microbenchmark(
#             `std::thread` = stdEmpty(.),
#             `RcppThread::Thread` = RcppThreadEmpty(.),
#             times = 1000
#         )
#     )
# ) %>%
#     unnest()
#
# medians <- timing %>%
#     group_by(threads, expr) %>%
#     summarize(ms = median(time / 10^6)) %>%
#     ungroup()
#
# medians %>%
#     ggplot(aes(threads, ms, color = expr, linetype = expr)) +
#     geom_line(size = 0.8) +
#     expand_limits(y = 0) +
#     labs(color = "", linetype = "") +
#     theme(legend.margin = margin(1, 1, 1, 1)) +
#     theme(legend.position = "bottom")
# ggsave("benchEmptyThread.pdf", width = 5.5, height = 3)
#
#
# ## Checking for interruptions
#
# ## Checking for user interruptions
#
# Rcpp::sourceCpp(code =
# "
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::depends(RcppThread)]]
# #include <RcppThread.h>
#
# // [[Rcpp::export]]
# void interruptMain(int nChecks)
# {
#     for (size_t i = 0; i < nChecks; ++i)
#         RcppThread::checkUserInterrupt();
# }
#
# // [[Rcpp::export]]
# void interruptChild(int nChecks)
# {
#     RcppThread::Thread thread([nChecks] {
#         for (size_t i = 0; i < nChecks; ++i)
#             RcppThread::checkUserInterrupt();
#     });
#     thread.join();
# }
# ")
#
# timing <- tibble(
#     interruptions = seq.int(0, 10^4, l = 10),
#     timings = map(
#         interruptions,
#         ~ microbenchmark(
#             `main thread` = interruptMain(.),
#             `child thread` = interruptChild(.),
#             times = 1000
#         )
#     )
# ) %>%
#     unnest()
#
# medians <- timing %>%
#     group_by(interruptions, expr) %>%
#     summarize(ms = median(time / 10^6))
#
# medians %>%
#     ggplot(aes(interruptions, ms, color = expr, linetype = expr)) +
#     geom_line(size = 0.8) +
#     expand_limits(y = 0) +
#     labs(linetype = "called from", color = "called from")  +
#     theme(legend.position = "bottom")
# ggsave("benchInterrupt.pdf", width = 5.5, height = 3)



# Benchmarking against other libraries -------------------------------------

## Empty jobs

Rcpp::sourceCpp(code =
"
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::plugins(openmp)]]
// [[Rcpp::depends(RcppThread)]]
// [[Rcpp::depends(RcppParallel)]]

#include <Rcpp.h>
#include <omp.h>
#include <RcppThread.h>
#include <RcppParallel.h>

// [[Rcpp::export]]
void singleThreaded(int n)
{
    for (size_t i = 0; i < n; ++i) ;
}

// [[Rcpp::export]]
void ThreadPool(int n)
{
    RcppThread::ThreadPool pool;
    for (size_t i = 0; i < n; ++i)
        pool.push([] {});
    pool.join();
}

// [[Rcpp::export]]
void parallelFor(int n)
{
    RcppThread::parallelFor(0, n, [] (int i) {});
}

// [[Rcpp::export]]
void OpenMP(int n)
{
    omp_set_num_threads(std::thread::hardware_concurrency());
    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i) ;
}

struct EmptyJob : public RcppParallel::Worker
{
    void operator()(std::size_t begin, std::size_t end) {
        for (size_t i = begin; i < end; i++) ([] {})();
    }
};

// [[Rcpp::export]]
void RcppParallelFor(int n)
{
    EmptyJob job{};
    RcppParallel::parallelFor(0, n, job);
}
")

timing <- tibble(
    jobs = seq.int(0, 10^4, l = 5),
    timings = map(
        jobs,
        ~ microbenchmark(
            `single threaded` = singleThreaded(.),
            ThreadPool = ThreadPool(.),
            parallelFor = parallelFor(.),
            OpenMP = OpenMP(.),
            RcppParallel = RcppParallelFor(.),
            times = 500
        )
    )
) %>%
    unnest()

medians <- timing %>%
    group_by(jobs, expr) %>%
    summarize(ms = median(time / 10^6))

medians %>%
    ggplot(aes(jobs, ms, color = expr, linetype = expr)) +
    geom_line(size = 0.8) +
    expand_limits(y = 0) +
    labs(linetype = "", color = "") +
    scale_y_log10()  +
    theme(legend.position = "bottom")
ggsave("benchEmpty.pdf", width = 6, height = 3)


## Fast iterations

Rcpp::sourceCpp(code =
                    "
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::plugins(openmp)]]
// [[Rcpp::depends(RcppThread)]]
// [[Rcpp::depends(RcppParallel)]]

#include <Rcpp.h>
#include <omp.h>
#include <RcppThread.h>
#include <RcppParallel.h>


double op(double x) {
    double xx = x;
    for (int j = 0; j != 1000; j++) {
        xx = std::sqrt(xx);
    }
    return xx;
}

// [[Rcpp::export]]
void singleThreaded(int n)
{
    std::vector<double> x(100000);
    for (size_t i = 0; i < n; ++i) x[i] = op(x[i]);
}

// [[Rcpp::export]]
void ThreadPool(int n)
{
    std::vector<double> x(100000);
    RcppThread::ThreadPool pool;
    for (size_t i = 0; i < n; ++i)
        pool.push([&, i] { x[i] = op(x[i]); });
    pool.join();
}

// [[Rcpp::export]]
void parallelFor(int n)
{
    std::vector<double> x(100000);
    RcppThread::parallelFor(0, n, [&] (int i) { x[i] = op(x[i]) ;});
}

// [[Rcpp::export]]
void OpenMP(int n)
{
    std::vector<double> x(100000);
    auto xx = x;
    omp_set_num_threads(std::thread::hardware_concurrency());
    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i) x[i] = op(x[i]);
}

struct FastJob : public RcppParallel::Worker
{
    const std::vector<double> input;
    std::vector<double> output;
    FastJob(std::vector<double>& input, std::vector<double> output) :
        input(input), output(output)
    {}

    void operator()(std::size_t begin, std::size_t end) {
        for (size_t i = begin; i < end; i++) output[i] = op(input[i]);
    }
};

// [[Rcpp::export]]
void RcppParallelFor(int n)
{
    std::vector<double> x(100000);
    auto xx = x;
    FastJob job{x, xx};
    RcppParallel::parallelFor(0, n, job);
}
")

timing <- tibble(
    jobs = seq.int(0, 10^4, l = 5),
    timings = map(
        jobs,
        ~ microbenchmark(
            `single threaded` = singleThreaded(.),
            ThreadPool = ThreadPool(.),
            parallelFor = parallelFor(.),
            OpenMP = OpenMP(.),
            RcppParallel = RcppParallelFor(.),
            times = 200
        )
    )
) %>%
    unnest()

medians <- timing %>%
    group_by(jobs, expr) %>%
    summarize(ms = median(time / 10^6))

medians %>%
    ggplot(aes(jobs, ms, color = expr, linetype = expr)) +
    geom_line(size = 0.8) +
    expand_limits(y = 0) +
    labs(linetype = "", color = "") +
    scale_y_log10()  +
    theme(legend.position = "bottom")
ggsave("benchFast.pdf", width = 6, height = 3)

## Kernel density estimates

Rcpp::sourceCpp(code =
"
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::plugins(openmp)]]
// [[Rcpp::depends(RcppThread)]]
// [[Rcpp::depends(RcppParallel)]]
// [[Rcpp::depends(RcppEigen)]]

#include <Rcpp.h>
#include <omp.h>
#include <RcppThread.h>
#include <RcppParallel.h>
#include <Eigen/Dense>

using namespace Eigen;

VectorXd kernel(const VectorXd& x)
{
    VectorXd k(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        if (std::abs(x(i)) > 1)
            k(i) = 0;
        else
            k(i) = 0.75 * (1 - std::pow(x(i), 2));
    }
    return k;
}

VectorXd kde(const VectorXd& x)
{
    double n = x.size();
    double sd = std::sqrt((x.array() - x.mean()).square().sum() / (n - 1));
    double bw = 1.06 * sd * std::pow(n, -0.2);
    VectorXd grid = VectorXd::LinSpaced(500, -1, 1);
    VectorXd fhat(grid.size());
    for (size_t i = 0; i < grid.size(); ++i) {
        fhat(i) = kernel((x.array() - grid(i)) / bw).mean() / bw;
    }

    return fhat;
}

// [[Rcpp::export]]
void singleThreaded(int n, int d)
{
    MatrixXd x = MatrixXd(n, d).setRandom();
    for (size_t i = 0; i < d; ++i)
        kde(x.col(i));
}

// [[Rcpp::export]]
void ThreadPool(int n, int d)
{
    MatrixXd x = MatrixXd(n, d).setRandom();
    RcppThread::ThreadPool pool;
    for (size_t i = 0; i < d; ++i)
        pool.push([&, i] { kde(x.col(i)); });
    pool.join();
}

// [[Rcpp::export]]
void parallelFor(int n, int d)
{
    MatrixXd x = MatrixXd(n, d).setRandom();
    RcppThread::parallelFor(0, d, [&] (size_t i) {
        kde(x.col(i));
    });
}

// [[Rcpp::export]]
void OpenMP(int n, int d)
{
    MatrixXd x = MatrixXd(n, d).setRandom();
    omp_set_num_threads(std::thread::hardware_concurrency());
    #pragma omp parallel for
    for (size_t i = 0; i < d; ++i) {
        kde(x.col(i));
        RcppThread::checkUserInterrupt();
    }
}

struct KDEJob : public RcppParallel::Worker
{
    KDEJob(const MatrixXd& x) : x_(x) {}

    void operator()(size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++) {
            RcppThread::checkUserInterrupt();
            kde(x_.col(i));
        }
    }

    const MatrixXd& x_;
};

// [[Rcpp::export]]
int RcppParallelFor(int n, int d)
{
    MatrixXd x = MatrixXd(n, d).setRandom();
    KDEJob job(x);
    RcppParallel::parallelFor(0, d, job);
    return 1;
}
")

timing <- crossing(
    n = c(10, 100, 500, 1000),
    d = c(10, 100)
) %>%
    mutate(
        timings = map2(
            n, d,
            ~ microbenchmark(
                `single threaded` = singleThreaded(.x, .y),
                ThreadPool = ThreadPool(.x, .y),
                parallelFor = parallelFor(.x, .y),
                OpenMP = OpenMP(.x, .y),
                RcppParallel = RcppParallelFor(.x, .y),
                times = 100
            )
        )
    ) %>%
    unnest()

medians <- timing %>%
    group_by(n, d, expr) %>%
    summarize(ms = median(time / 10^6)) %>%
    ungroup()

medians %>%
    mutate(d = str_c("d = ", d)) %>%
    ggplot(aes(n, ms, color = expr, linetype = expr)) +
    facet_wrap(~ d, scales = "free_y") +
    geom_line(size = 0.8) +
    expand_limits(y = 0) +
    labs(linetype = "", color = "") +
    xlab("sample size")  +
    theme(legend.position = "bottom")
ggsave("benchKDE.pdf", width = 8, height = 8)

#
# ## Kendall correlation matrix
#
# Rcpp::sourceCpp(code =
# '
# // [[Rcpp::plugins(cpp11)]]
# // [[Rcpp::plugins(openmp)]]
# // [[Rcpp::depends(RcppThread)]]
# // [[Rcpp::depends(RcppParallel)]]
# // [[Rcpp::depends(wdm)]]
# // [[Rcpp::depends(RcppEigen)]]
#
# #include <wdm/eigen.hpp>
# #include <omp.h>
# #include <RcppThread.h>
# #include <RcppParallel.h>
# #include <RcppEigen.h>
#
# using namespace Eigen;
#
# void computeKendall(size_t i, size_t j, const MatrixXd& mat, MatrixXd& cor)
# {
#     cor(i, j) = wdm::wdm(mat.col(i), mat.col(j), "kendall");
#     cor(j, i) = cor(i, j);
# }
#
# // [[Rcpp::export]]
# void singleThreaded(int n, int d)
# {
#     MatrixXd mat = MatrixXd(n, d).setRandom();
#     MatrixXd cor = MatrixXd::Identity(d, d);
#     for (size_t i = 0; i < d; ++i) {
#         for (size_t j = i + 1; j < d; ++j) {
#             computeKendall(i, j, mat, cor);
#         }
#     }
# }
#
# // [[Rcpp::export]]
# void ThreadPool(int n, int d)
# {
#     MatrixXd mat = MatrixXd(n, d).setRandom();
#     MatrixXd cor = MatrixXd::Identity(d, d);
#
#     RcppThread::ThreadPool pool;
#     for (size_t i = 0; i < d; ++i) {
#         for (size_t j = i + 1; j < d; ++j) {
#             pool.push([&, i, j] { computeKendall(i, j, mat, cor); });
#         }
#     }
#     pool.join();
# }
#
# // [[Rcpp::export]]
# void parallelFor(int n, int d)
# {
#     MatrixXd mat = MatrixXd(n, d).setRandom();
#     MatrixXd cor = MatrixXd::Identity(d, d);
#
#     RcppThread::parallelFor(0, d, [&] (size_t i) {
#         for (size_t j = i + 1; j < d; ++j) {
#             computeKendall(i, j, mat, cor);
#         }
#     });
# }
#
# // [[Rcpp::export]]
# void OpenMP(int n, int d)
# {
#     MatrixXd mat = MatrixXd(n, d).setRandom();
#     MatrixXd cor = MatrixXd::Identity(d, d);
#
#     omp_set_num_threads(std::thread::hardware_concurrency());
#     #pragma omp parallel for
#     for (size_t i = 0; i < d; ++i) {
#         for (size_t j = i + 1; j < d; ++j) {
#             computeKendall(i, j, mat, cor);
#         }
#     }
# }
#
# struct CorJob : public RcppParallel::Worker
# {
#     CorJob(const MatrixXd& mat, MatrixXd& cor)
#     : mat_(mat), cor_(cor), d_(cor.cols()) {}
#
#     void operator()(size_t begin, size_t end) {
#         for (size_t i = begin; i < end; i++) {
#             for (size_t j = i + 1; j < d_; ++j) {
#                 computeKendall(i, j, mat_, cor_);
#             }
#         }
#     }
#
#     const MatrixXd& mat_;
#     MatrixXd& cor_;
#     size_t d_;
# };
#
# // [[Rcpp::export]]
# void RcppParallelFor(int n, int d)
# {
#     MatrixXd mat = MatrixXd(n, d).setRandom();
#     MatrixXd cor = MatrixXd::Identity(d, d);
#
#     CorJob job(mat, cor);
#     RcppParallel::parallelFor(0, d, job);
# }
# ')
#
# timing <- crossing(
#     n = c(10, 100, 500, 1000),
#     d = c(10, 100)
# ) %>%
#     mutate(
#         timings = map2(
#             n, d,
#             ~ microbenchmark(
#                 `single threaded` =  singleThreaded(.x, .y),
#                 ThreadPool = ThreadPool(.x, .y),
#                 parallelFor = parallelFor(.x, .y),
#                 OpenMP = OpenMP(.x, .y),
#                 RcppParallelFor = RcppParallelFor(.x, .y),
#                 times = 50
#             )
#         )
#     ) %>%
#     unnest()
#
# medians <- timing %>%
#     group_by(n, d, expr) %>%
#     summarize(ms = median(time / 10^6)) %>%
#     ungroup()
#
# medians %>%
#     mutate(d = str_c("d = ", d)) %>%
#     ggplot(aes(n, ms, color = expr, linetype = expr)) +
#     facet_wrap(~ d, scales = "free_y") +
#     geom_line(size = 0.8) +
#     expand_limits(y = 0) +
#     labs(linetype = "", color = "") +
#     xlab("sample size")  +
#     theme(legend.position = "bottom")
# ggsave("benchKendall.pdf", width = 8, height = 8)

