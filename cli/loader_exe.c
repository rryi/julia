#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

/* Bring in definitions for `PATH_MAX` and `PATHSEPSTRING`, `jl_ptls_t`, etc... */
#include "../src/julia.h"

#ifdef _OS_WINDOWS_
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <dlfcn.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Declarations from `loader_lib.c`
extern const char * get_exe_dir();
extern int load_repl(const char *, int, char **);


/* Define ptls getter, as this cannot be defined within a shared library. */
#if !defined(_OS_WINDOWS_) && !defined(_OS_DARWIN_)
JL_DLLEXPORT JL_CONST_FUNC jl_ptls_t jl_get_ptls_states_static(void)
{
    static __attribute__((tls_model("local-exec"))) __thread jl_tls_states_t tls_states;
    return &tls_states;
}
#endif

#ifdef _OS_WINDOWS_
int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
#else
int main(int argc, char * argv[])
#endif
{
    // Immediately get the current exe dir, allowing us to calculate relative paths.
    const char * exe_dir = get_exe_dir();

#ifdef _OS_WINDOWS_
    // Convert Windows wchar_t values to UTF8
    for (int i=0; i<argc; i++) {
        char * new_argv_i = NULL;
        if (!wchar_to_utf8(argv[i], &new_argv_i)) {
            fprintf(stderr, "Unable to convert %d'th argument to UTF-8!\n", i);
            return 1;
        }
        argv[i] = (wchar_t *)new_argv_i;
    }
#endif

    // Call load_repl with our initialization arguments:
    return load_repl(exe_dir, argc, (char **)argv);
}

#ifdef __cplusplus
} // extern "C"
#endif