context("R-monitor")
test_that("R-monitor works", {
    expect_output(RcppThread:::testMonitor(), "RcppThread says hi!", fixed = TRUE)
})

context("Thread class")
test_that("Thread class works", {
    expect_output(RcppThread:::testThreadClass())
})

context("Thread pool")
test_that("Thread pool works", {
    expect_output(RcppThread:::testThreadPool())
    expect_output(RcppThread:::testWait())
    expect_output(RcppThread:::testSingleThreaded())
})
