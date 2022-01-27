inlineCxxPlugin <- function(...) {
    plugin <- Rcpp::Rcpp.plugin.maker(
        include.before = "#include <RcppThread.h>",
        libs           = "-latomic",
        package        = "RcppThread"
    )
    settings <- plugin()
    settings$env$USE_CXX11 <- "yes"
    settings
}
