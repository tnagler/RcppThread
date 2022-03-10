checkForLibAtomic <- function() {
    if (.Platform$OS.type == "windows")
        return(FALSE)

    fl <- tempfile("test", fileext = ".cpp")
    cat(
        "#include <atomic>
        std::atomic<int> x;
        int main() {
            std::atomic_is_lock_free(&x);
            return 0;
        }",
        file = fl
    )

    compiler <- tools::Rcmd(c("config", "CXX"), stdout = TRUE)
    failed <- system(paste(compiler, fl, "-latomic"),
                     ignore.stdout = TRUE,
                     ignore.stderr = TRUE)
    unlink(fl)

    return(!failed)
}

hasAtomicSupport <- function() {
    if (checkForLibAtomic())
        return(TRUE)

    fl <- tempfile("test", fileext = ".cpp")
    cat(
        "#include <atomic>
        std::atomic<int> x;
        int main() {
            std::atomic_is_lock_free(&x);
            return 0;
        }",
        file = fl
    )
    compiler <- tools::Rcmd(c("config", "CXX"), stdout = TRUE)
    failed <- system(paste(compiler, fl),
                     ignore.stdout = TRUE,
                     ignore.stderr = TRUE)
    unlink(fl)

    return(!failed)
}

checkForLibPthread <- function() {
    if (.Platform$OS.type == "windows")
        return(FALSE)

    fl <- tempfile("test", fileext = ".cpp")
    cat(
        "#include <pthread.h>
        int main() {
             return 0;
        }",
        file = fl)

    compiler <- system("R CMD config CXX", intern = TRUE)
    failed <- system(paste(compiler, fl, "-lpthread"),
                     ignore.stdout = TRUE,
                     ignore.stderr = TRUE)
    unlink(fl)
    return(!failed)
}


#' Get portable linker flags for libraries building on RcppThread
#'
#' To be used in `Makevars` on Linux and OSX. Returns a character vector with
#' linker flags for `pthread` and `libatomic`, if available.
#'
#' Use as
#' `PKG_LIBS = ${R_HOME}/bin/Rscript -e 'RcppThread::LdFlags()'.
#'
#' @export
LdFlags <- function() {
    flags <- ""
    if (checkForLibAtomic())
        flags <- paste(flags, "-latomic")
    if (checkForLibPthread())
        flags <- paste(flags, "-lpthread")
    cat(flags)
    invisible(flags)
}
