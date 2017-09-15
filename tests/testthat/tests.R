context("R-monitor")
test_that("R-monitor works", {
    expect_output(RcppThreads:::testMonitor(), "RcppThreads says hi!")
})

context("Thread class")
test_that("Thread class works", {
    expect_output(RcppThreads:::testThreadClass(), "1234")
})
