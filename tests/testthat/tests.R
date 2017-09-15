context("R-monitor")

test_that("R-monitor works", {
    expect_output(RcppThreads:::testMonitor(), "RcppThreads says hi!")
})
