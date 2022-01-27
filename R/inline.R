inlineCxxPlugin <- function(...) {
    plugin <- Rcpp::Rcpp.plugin.maker(
        include.before  = "#include <RcppThread.h>",
        package        = "RcppThread",
        Makevars = "CXX += -stdlib=libstdc++"
    )
    settings <- plugin()
    settings$env$USE_CXX11 <- "yes"
    settings
}

