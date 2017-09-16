context("R-monitor")
test_that("R-monitor works", {
    expect_output(RcppThreads:::testMonitor(), "RcppThreads says hi!", fixed = TRUE)
})

context("Thread class")
test_that("Thread class works", {
    expect_output(RcppThreads:::testThreadClass(), "1234", fixed = TRUE)
})

context("Thread pool")
test_that("Thread pool works", {
    expect_output(RcppThreads:::testThreadPool())
})
