library(testthat)
library(RcppThread)

test_check("RcppThread")

my_print <- function(txt) {
    time <- Sys.time()
    msg <- paste0("[", time, "]  ", txt)
    cat(msg)
}

my_print("DOOOONE")