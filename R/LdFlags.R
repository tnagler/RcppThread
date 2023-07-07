getCompiler <- function() {
    tools::Rcmd(c("config", "CXX"), stdout = TRUE)
}

runCmd <- function(...) {
    system(paste(...), ignore.stdout = TRUE, ignore.stderr = TRUE)
}

createTestFiles <- function() {
    src <- tempfile("test", fileext = ".cpp")
    out <- tempfile("test", fileext = ".out")
    c(src = src, out = out)
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

    tmp <- createTestFiles()
    writeLibAtomicTest(tmp["src"])
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["out"], "-latomic")
    unlink(tmp)

    !failed
}

hasAtomicSupport <- function() {
    if (checkForLibAtomic())
        return(TRUE)

    tmp <- createTestFiles()
    writeLibAtomicTest(tmp["src"])
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["out"])
    unlink(tmp)

    !failed
}

checkForLibPthread <- function() {
    if (.Platform$OS.type == "windows")
        return(FALSE)

    tmp <- createTestFiles()
    cat("#include <pthread.h> \n int main() { return 0; }", file = tmp["src"])
    failed <- runCmd(getCompiler(), tmp["src"], "-o", tmp["out"], "-lpthread")
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


#' Internal test for global thread pool
testGlobal <- function() {
    .Call("testGlobalCpp")
}

