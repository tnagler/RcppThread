// #include "RcppThread.h"
//
// extern "C" {
//
// SEXP testGlobalCpp() {
//     RcppThread::
//     SEXP result;
//     PROTECT(result = NEW_INTEGER(1));
//     INTEGER(result)[0] = std::thread::hardware_concurrency();
//     UNPROTECT(1);
//     return result;
// }
//
//
// static const R_CallMethodDef callMethods[] = {
//     {"testGlobalCpp", (DL_FUNC) &detectCoresCpp, 0},
//     {NULL, NULL, 0}
// };
//
// void R_init_RcppThread(DllInfo *info)
// {
//     R_registerRoutines(info, NULL, callMethods, NULL, NULL);
//     R_useDynamicSymbols(info, TRUE);
// }
//
// }
