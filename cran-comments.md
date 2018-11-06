## Urgent patch: failure of CRAN checks

Patch following failure of CRAN checks on some platforms. The reason was
a non-portable type declaration in the C++ code. I fixed it by using
automatic type deduction (verified on multiple platforms, see below).

## Test environments
* ubuntu 12.04 (release, devel)
* win-builder (release, devel)
* OSX on travis and r-hub
* debian linux on r-hub
* fedora on r-hub

## R CMD check results

0 errors | 0 warnings | 1 notes

* Days since last update: 3

## Reverse dependencies

rvinecopulib: 0 errors | 0 warnings | 0 notes
