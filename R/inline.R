inlineCxxPlugin <- function(...) {
    libs <- if (.Platform$OS.type == "unix") "-lpthread -DAFFINITY" else ""
    libs <- paste(libs, "-latomic")
    plugin <- Rcpp::Rcpp.plugin.maker(
        include.before = "#include <RcppThread.h>",
        libs           = libs,
        package        = "RcppThread"
    )
    settings <- plugin()
    settings$env$PKG_CPPFLAGS <- paste("-I../inst/include", libs)
    settings$env$USE_CXX11 <- "yes"
    settings
}
