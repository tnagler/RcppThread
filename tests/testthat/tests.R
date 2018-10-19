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

test_that("nested parallelFor works", {
    expect_silent(RcppThread:::testThreadPoolNestedParallelFor())
})

test_that("parallelForEach works", {
    expect_silent(RcppThread:::testThreadPoolParallelForEach())
})

test_that("nested parallelForEach works", {
    expect_silent(RcppThread:::testThreadPoolNestedParallelForEach())
})


test_that("works single threaded", {
    expect_silent(RcppThread:::testThreadPoolSingleThreaded())
})


## -------------------------------------------------------
context("Parallel for functions")
test_that("parallelFor works", {
    expect_silent(RcppThread:::testParallelFor())
})

test_that("nested parallelFor works", {
    expect_silent(RcppThread:::testNestedParallelFor())
})

test_that("parallelForEach works", {
    expect_silent(RcppThread:::testParallelForEach())
})

test_that("nested parallelForEach works", {
    expect_silent(RcppThread:::testNestedParallelForEach())
})
