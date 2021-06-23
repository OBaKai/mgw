#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>


int main(int argc, char *argv[])
{
    int (*dl_func)(int, int);
    void *handle = NULL;
    char *error = NULL;


    handle = dlopen("dl_func.so", RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "dlopen dl_func.so failed!\n");
        return -1;
    }

    dlerror();      /* clear error informations */

    /* the C99 standard leaves casting from "void *" to a function pointer undefined. */
    *(void **)(&dl_func) = dlsym(handle, "add");
    if ((error = dlerror()) != NULL)
    {
        fprintf(stderr, "dlsym add failed!\n");
        return -2;
    }

    printf("[%s]: dl_func->add(%d, %d): %d\n", __FILE__, 4, 9, (*dl_func)(4, 9));

    *(void **)(&dl_func) = dlsym(handle, "sub");
    if ((error = dlerror()) != NULL)
    {
        fprintf(stderr, "dlsym sub failed!\n");
        return -2;
    }

    printf("[%s]: dl_func->sub(%d, %d): %d\n", __FILE__, 56, 6, (*dl_func)(56, 6));

    dlclose(handle);

    return 0;
}