inlineCxxPlugin <- function(...) {
    settings <- Rcpp::Rcpp.plugin.maker(
        include.before  = "#include <RcppThread.h>",
        package = "RcppThread",
        libs = RcppThread::LdFlags()
    )()
    settings$env$USE_CXX11 <- "yes"
    settings
}

