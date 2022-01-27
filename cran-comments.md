Fixes build warning on windows about misaligned address/allocation. Hopefully
also the problems on Fedora, which I could not reproduce.

## Test environments
* ubuntu 20.04 (devel, release, old-rel)
* macOS X (release)
* Windows Server 2019 (release)
* CRAN win builder (devel)
* rhub::check_for_cran()
* rhub::check_with_sanitizers()
* rhub::check_with_valgrind()

## Check status summary
                WARN NOTE OK
Source packages    0    0  1
Reverse depends    1   17  1

The warning in package CDSeq is known and expected.
