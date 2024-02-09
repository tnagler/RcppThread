#' R-friendly C++11 threads
#'
#' Provides a C++11-style thread class and thread pool that can safely be
#' interrupted from R.
#'
#' @references Nagler, T. (2021). "R-Friendly Multi-Threading in C++."
#' _Journal of Statistical Software, Code Snippets_, *97*(1), 1-18.
#' \doi{10.18637/jss.v097.c01}.
#'
#' @name RcppThread
#' @docType package
#' @useDynLib RcppThread, .registration=TRUE
"_PACKAGE"
