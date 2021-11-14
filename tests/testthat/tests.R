## -------------------------------------------------------
context("Compile test functions")
Rcpp::sourceCpp(file = normalizePath("../tests.cpp"))


runs <- 1
for (run in seq_len(runs)) {
    context(paste0("---------------------------- run ", run, "/", runs))
    test_that("start", expect_true(TRUE))

    ## -------------------------------------------------------
    context("R-monitor")
    test_that("R-monitor works", {
        expect_output(testMonitor(), "RcppThread says hi!", fixed = TRUE)
    })


    ## -------------------------------------------------------
    context("Thread class")
    test_that("Thread class works", {
        expect_output(testThreadClass())
    })


    ## -------------------------------------------------------
    context("Thread pool")
    test_that("push works", {
        expect_silent(testThreadPoolPush())
    })

    test_that("pushReturn works", {
        expect_silent(testThreadPoolPushReturn())
    })

    test_that("map works", {
        expect_silent(testThreadPoolMap())
    })

    test_that("parallelFor works", {
        expect_silent(testThreadPoolParallelFor())
    })

    test_that("nested parallelFor works", {
        expect_silent(testThreadPoolNestedParallelFor())
    })

    test_that("parallelForEach works", {
        expect_silent(testThreadPoolParallelForEach())
    })

    test_that("nested parallelForEach works", {
        expect_silent(testThreadPoolNestedParallelForEach())
    })

    test_that("works single threaded", {
        expect_silent(testThreadPoolSingleThreaded())
    })

    test_that("destructible without join", {
        expect_silent(testThreadPoolDestructWOJoin())
    })


    ## -------------------------------------------------------
    context("Parallel for functions")
    test_that("parallelFor works", {
        expect_silent(testParallelFor())
    })

    test_that("nested parallelFor works", {
        expect_silent(testNestedParallelFor())
    })

    test_that("parallelForEach works", {
        expect_silent(testParallelForEach())
    })

    test_that("nested parallelForEach works", {
        expect_silent(testNestedParallelForEach())
    })


    ## ------------------------------------------------------
    context("Progress tracking")
    test_that("ProgressCounter works", {
        expect_output(testProgressCounter(), "100% \\(done\\)")
    })

    test_that("ProgressBar works", {
        expect_output(testProgressBar(), "100% \\(done\\)")
    })
}
