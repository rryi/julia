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

/*
 * DEP_LIBS is our list of dependent libraries that must be loaded before `libjulia`.
 * Note that order matters, as each entry will be opened in-order.  We define here a
 * dummy value just so this file compiles on its own, and also so that developers can
 * see what this value should look like.  Note that the last entry must always be
 * `libjulia`, and that all paths should be relative to this loader `.exe` path.
 */
#if !defined(DEP_LIBS)
#define DEP_LIBS "../lib/example.so:../lib/libjulia.so"
#endif
static char dep_libs[256] = DEP_LIBS;

/* Utilities to convert from Windows' wchar_t stuff to UTF-8 */
#ifdef _OS_WINDOWS_
static int wchar_to_utf8(wchar_t * wstr, char **str) {
    size_t len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (!len)
        return 1;

    *str = (char *)alloca(len);
    if (!WideCharToMultiByte(CP_UTF8, 0, wstr, -1, *str, len, NULL, NULL))
        return 1;
    return 0;
}

static int utf8_to_wchar(char * str, wchar_t ** wstr) {
    size_t len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (!len)
        return 1;
    *wstr = (wchar_t *)alloca(len * sizeof(wchar_t));
    if (!MultiByteToWideChar(CP_UTF8, 0, str, -1, *wstr, len))
        return 1;
    return 0;
}
#endif


/* Absolute path to the path of the current executable, gets filled in by `get_exe_path()` */
static void * load_library(const char * rel_path, const char * src_dir) {
    char path[2*PATH_MAX + 1];
    snprintf(path, sizeof(path)/sizeof(char), "%s%s%s", src_dir, PATHSEPSTRING, rel_path);

    void * handle = NULL;
#if defined(_OS_WINDOWS_)
    wchar_t * wpath = NULL;
    if (!utf8_to_wchar(path, &wpath)) {
        fprintf(stderr, "ERROR: Unable to convert path %s to wide string!\n", path);
        exit(1);
    }
    handle = (void *)LoadLibraryExW(wpath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif

    if (handle == NULL) {
        fprintf(stderr, "ERROR: Unable to load dependent library %s\n", path);
#if defined(_OS_WINDOWS_)
        char err[256];
        win32_formatmessage(GetLastError(), err, sizeof(err));
        fprintf(stderr, "%s\n", err);
#else
        fprintf(stderr, "%s\n", dlerror());
#endif
        exit(1);
    }
    return handle;
}

char * exe_dir = NULL;
char * get_exe_dir()
{
#if defined(_OS_WINDOWS_)
    // On Windows, we use GetModuleFileName()
    wchar_t julia_path[PATH_MAX];
    if (!GetModuleFileName(NULL, julia_path, PATH_MAX)) {
        fprintf(stderr, "ERROR: GetModuleFileName() failed with code %lu\n", GetLastError());
        exit(1);
    }
    if (!wchar_to_utf8(julia_path, &exe_dir)) {
        fprintf(stderr, "ERROR: Unable to convert julia path to UTF-8\n");
        exit(1);
    }
#elif defined(_OS_DARWIN_)
    // On MacOS, we use _NSGetExecutablePath(), followed by realpath()
    char nonreal_exe_path[PATH_MAX + 1];
    uint32_t exe_path_len = PATH_MAX;
    int ret = _NSGetExecutablePath(nonreal_exe_path, &exe_path_len);
    if (!ret) {
        fprintf(stderr, "ERROR: _NSGetExecutablePath() returned %d\n", ret);
        exit(1);
    }

    /* realpath(nonreal_exe_path) may be > PATH_MAX so double it to be on the safe side. */
    exe_dir = (char *)malloc(2*PATH_MAX + 1);
    if (realpath(nonreal_exe_path, exe_dir) == NULL) {
        fprintf(stderr, "ERROR: realpath() failed with code %d\n", errno);
        exit(1);
    }
#elif defined(_OS_LINUX_)
    // On Linux, we read from /proc/self/exe
    exe_dir = (char *)malloc(2*PATH_MAX + 1);
    int num_bytes = readlink("/proc/self/exe", exe_dir, PATH_MAX);
    if (num_bytes == -1) {
        fprintf(stderr, "ERROR: readlink(/proc/self/exe) failed with code %d\n", errno);
        exit(1);
    }
    exe_dir[num_bytes] = '\0';
#elif defined(_OS_FREEBSD_)
    // On FreeBSD, we use the KERN_PROC_PATHNAME sysctl:
    exe_dir = (char *) malloc(2*PATH_MAX + 1);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    int exe_dir_len = 2*PATH_MAX;
    int ret = sysctl(mib, 4, exe_dir, &exe_dir_len, NULL, 0);
    if (ret) {
        fprintf(stderr, "ERROR: sysctl(KERN_PROC_PATHNAME) failed with code %d\n", ret);
        exit(1);
    }
    exe_dir[exe_dir_len] = '\0';
#endif
    // Finally, convert to dirname
    return dirname(exe_dir);
}

// Load libjulia and run the REPL with the given arguments (in UTF-8 format)
int load_repl(const char * exe_dir, int argc, char * argv[])
{
    // Pre-load libraries that libjulia needs.
    int deps_len = strlen(dep_libs);
    char * curr_dep = &dep_libs[0];
    while (1) {
        // try to find next colon character, if we can't, escape out.
        char * colon = strchr(curr_dep, ':');
        if (colon == NULL)
            break;

        // Chop the string at the colon, load this library.
        *colon = '\0';
        load_library(curr_dep, exe_dir);

        // Skip ahead to next dependency
        curr_dep = colon + 1;
    }

    // Last dependency is `libjulia`, so load that and we're done with `dep_libs`!
    void * libjulia = load_library(curr_dep, exe_dir);

    // Next, if we're on Linux/FreeBSD, set up fast TLS.
#if !defined(_OS_WINDOWS_) && !defined(_OS_DARWIN_)
    void (*jl_set_ptls_states_getter)(jl_get_ptls_states_func) = dlsym(libjulia, "jl_set_ptls_states_getter");
    if (jl_set_ptls_states_getter == NULL) {
        fprintf(stderr, "ERROR: Cannot find jl_set_ptls_states_getter() function within libjulia!\n");
        exit(1);
    }
    jl_get_ptls_states_func fptr = dlsym(NULL, "jl_get_ptls_states_static");
    if (fptr == NULL) {
        fprintf(stderr, "ERROR: Cannot find jl_get_ptls_states_static(), must define this symbol within calling executable!\n");
        exit(1);
    }
    jl_set_ptls_states_getter(fptr);
#endif

    // Load the repl entrypoint symbol and jump into it!
    int (*entrypoint)(int, char **) = NULL;
    #ifdef _OS_WINDOWS_
        entrypoint = (int (*)(int, char **))GetProcAddress((HMODULE) libjulia, "repl_entrypoint");
    #else
        entrypoint = (int (*)(int, char **))dlsym(libjulia, "repl_entrypoint");
    #endif
    if (entrypoint == NULL) {
        fprintf(stderr, "ERROR: Unable to find `repl_entrypoint()` within libjulia!\n");
        exit(1);
    }
    return entrypoint(argc, (char **)argv);
}

#ifdef __cplusplus
} // extern "C"
#endif