getCompiler <- function() {
    tools::Rcmd(c("config", "CXX"), stdout = TRUE)
}

runCmd <- function(...) {
    args <- list(command = paste(...))
    args$ignore.stdout = TRUE
    args$ignore.stderr = TRUE
    do.call(system, args)
}

createTestFiles <- function() {
    src <- tempfile("test", fileext = ".cpp")
    lib <- tempfile("test", fileext = ".out")
    c(src = src, lib = lib)
}

writeLibAtomicTest <- function(file) {
    cat(
        "#include <atomic>
        std::atomic<int> x;
        int main() {
            std::atomic_is_lock_free(&x);
            return 0;
        }",
        file = file
    )
}

checkForLibAtomic <- function() {
    if (.Platform$OS.type == "windows")
        return(TRUE)

    # create temporary source and out files
    tmp <- createTestFiles()
    writeLibAtomicTest(tmp["src"])

    # check whether test program can be compiled
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["lib"], "-latomic")

    # clean up temporary files
    unlink(tmp)

    !failed
}

hasAtomicSupport <- function() {
    if (checkForLibAtomic())
        return(TRUE)

    # create temporary source and out files
    tmp <- createTestFiles()
    writeLibAtomicTest(tmp["src"])

    # check whether test program can be compiled
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["lib"])

    # clean up temporary files
    unlink(tmp)

    !failed
}

checkForLibPthread <- function() {
    if (.Platform$OS.type == "windows")
        return(FALSE)

    # create temporary source and out files
    tmp <- createTestFiles()
    cat("#include <pthread.h> \n int main() { return 0; }", file = tmp["src"])

    # check whether test program can be compiled
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["lib"], "-lpthread")

    # clean up temporary files
    unlink(tmp)

    !failed
}


#' Get portable linker flags for libraries building on RcppThread
#'
#' To be used in `Makevars` on Linux and OSX. Returns a string with
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
