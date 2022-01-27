inlineCxxPlugin <- function(...) {
    libs <- if (.Platform$OS.type == "unix") "-latomic" else ""
    settings <- Rcpp::Rcpp.plugin.maker(
        include.before  = "#include <RcppThread.h>",
        package = "RcppThread",
        libs = libs,
    )()
    settings$env$USE_CXX11 <- "yes"
    settings
}

