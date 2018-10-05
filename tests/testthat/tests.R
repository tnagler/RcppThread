## -------------------------------------------------------
context("R-monitor")
test_that("R-monitor works", {
    expect_output(RcppThread:::testMonitor(), "RcppThread says hi!", fixed = TRUE)
})


## -------------------------------------------------------
context("Thread class")
test_that("Thread class works", {
    expect_output(RcppThread:::testThreadClass())
})


## -------------------------------------------------------
context("Thread pool")
test_that("push works", {
    expect_silent(RcppThread:::testThreadPoolPush())
})

test_that("push works", {
    expect_silent(RcppThread:::testThreadPoolPush())
})

test_that("map works", {
    expect_silent(RcppThread:::testThreadPoolMap())
})

test_that("parallelFor works", {
    expect_silent(RcppThread:::testThreadPoolParallelFor())
})

test_that("forEach works", {
    expect_silent(RcppThread:::testThreadPoolForEach())
})

test_that("works single threaded", {
    expect_silent(RcppThread:::testThreadPoolSingleThreaded())
})


## -------------------------------------------------------
context("Parallel for functions")
test_that("parallelFor works", {
    expect_silent(RcppThread:::testParallelFor())
})

test_that("parallelForEach works", {
    expect_silent(RcppThread:::testForEach())
})
