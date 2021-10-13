#' Detect the Number of CPU Cores
#'
#' Detects the number of (logical) CPU cores.
#'
#' @export
detectCores <- function() {
    .Call("detectCoresCpp")
}
