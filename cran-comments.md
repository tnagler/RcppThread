## Test environments
* ubuntu 20.04 (devel, release, old-rel)
* macOS X (release)
* Windows Server 2019 (release)
* CRAN win builder (devel)
* rhub::check_for_cran()
* rhub::check_with_sanitizers()
* rhub::check_with_valgrind()

Check status summary:
                  WARN NOTE OK
  Source packages    0    0  1
  Reverse depends    1   17  1

There was 1 WARNING in package CDSeq:

"Warning: replacing previous import ‘RcppThread::detectCores’ by ‘parallel::detectCores’ when loading ‘CDSeq’"

This is caused by CDSeq importing both 'parallel' and 'RcppThread' namespaces in full. I have opened a pull request to fix this in November: https://github.com/kkang7/CDSeq_R_Package/pull/13. I have also sent an email to the maintainer last week, with no response.
