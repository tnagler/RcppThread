#include <thread>

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif

#include <R.h>
#include <Rdefines.h>

#include "RcppThread.h"

extern "C"
{

    SEXP detectCoresCpp()
    {
        SEXP result;
        PROTECT(result = NEW_INTEGER(1));
        INTEGER(result)[0] = std::thread::hardware_concurrency();
        UNPROTECT(1);
        return result;
    }

    SEXP testGlobalCpp()
    {
        auto fun = [](int i) {};
        RcppThread::parallelFor(0, 100, fun);
        SEXP result;
        PROTECT(result = NEW_INTEGER(1));
        INTEGER(result)[0] = 1;
        UNPROTECT(1);
        return result;
    }

    static const R_CallMethodDef callMethods[] = {
        { "detectCoresCpp", (DL_FUNC)&detectCoresCpp, 0 },
        { "testGlobalCpp", (DL_FUNC)&testGlobalCpp, 0 },
        { NULL, NULL, 0 }
    };

    void R_init_RcppThread(DllInfo* info)
    {
        R_registerRoutines(info, NULL, callMethods, NULL, NULL);
        R_useDynamicSymbols(info, TRUE);
    }
}
