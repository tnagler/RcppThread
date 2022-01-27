inlineCxxPlugin <- function(...) {
    settings <- Rcpp::Rcpp.plugin.maker(
        include.before  = "#include <RcppThread.h>",
        package = "RcppThread",
        libs = if (Sys.info()["sysname"] == "Linux") "-latomic -lpthread" else ""
    )()
    settings$env$USE_CXX11 <- "yes"
    settings
}

