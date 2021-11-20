## -------------------------------------------------------
context("Compile test functions")
Rcpp::sourceCpp(file = normalizePath("../tests.cpp"))

my_print <- function(txt) {
    time <- Sys.time()
    msg <- paste0("[", time "]  ", txt)
    cat(msg)
}


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
    my_print("push works \n")

    test_that("push works", {
        testThreadPoolPush()
        expect_silent(testThreadPoolPush())
    })
    my_print("pushReturn works \n")

    test_that("pushReturn works", {
        expect_silent(testThreadPoolPushReturn())
    })
    my_print("map works \n")

    test_that("map works", {
        expect_silent(testThreadPoolMap())
    })
    my_print("parallelFor works \n")

    test_that("parallelFor works", {
        expect_silent(testThreadPoolParallelFor())
    })

    my_print("nested parallelFor works \n")
    test_that("nested parallelFor works", {
        expect_silent(testThreadPoolNestedParallelFor())
    })
    my_print("parallelForEach works \n")

    test_that("parallelForEach works", {
        expect_silent(testThreadPoolParallelForEach())
    })
    
    my_print("nested parallelForEach works \n")
    test_that("nested parallelForEach works", {
        expect_silent(testThreadPoolNestedParallelForEach())
    })
    my_print("works single threaded \n")

    test_that("works single threaded", {
        expect_silent(testThreadPoolSingleThreaded())
    })
    my_print("destructible without join \n")

    test_that("destructible without join", {
        expect_silent(testThreadPoolDestructWOJoin())
    })
    
    
    ## -------------------------------------------------------
    context("Parallel for functions")

    my_print("parallelFor works\n")
    test_that("parallelFor works", {
        expect_silent(testParallelFor())
    })
    
    my_print("nested parallelFor works\n")
    test_that("nested parallelFor works", {
        expect_silent(testNestedParallelFor())
    })
    

    my_print("parallelForEach works\n")
    test_that("parallelForEach works", {
        expect_silent(testParallelForEach())
    })
    
    
    test_that("nested parallelForEach works", {
        expect_silent(testNestedParallelForEach())
    })
    my_print("nested parallelForEach works\n")
    
    ## ------------------------------------------------------
    my_print("progress tracking\n")
    context("Progress tracking")
    test_that("ProgressCounter works", {
        expect_output(testProgressCounter(), "100% \\(done\\)")
    })
    
    test_that("ProgressBar works", {
        expect_output(testProgressBar(), "100% \\(done\\)")
    })

    my_print("done with test run\n")
}

my_print("done testing\n")
