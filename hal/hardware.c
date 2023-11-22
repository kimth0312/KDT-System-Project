#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>
#include <hardware.h>
#include <stdlib.h>

#define HAL_LIBRARY_PATH1 "./libcamera.so"

static int load(const struct hw_module_t **pHmi)
{
    int status;
    void *handle;
    struct hw_module_t *hmi;

    handle = dlopen(HAL_LIBRARY_PATH1, RTLD_LAZY);
    if (handle == NULL)
    {
        perror("dlopen error");
        exit(-1);
    }

    (void)dlerror();

    const char *sym = HAL_MODULE_INFO_SYM_AS_STR; // "HMI"
    // .so 파일에서 struct hw_module_t 인 symbol을 찾아서 대입
    hmi = (struct hw_module_t *)dlsym(handle, sym);
    *pHmi = hmi;

    return status;
}

int hw_get_camera_module(const struct hw_module_t **module)
{
    return load(module);
}