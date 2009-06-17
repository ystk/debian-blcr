#define _GNU_SOURCE 1 // For RTLD_DEFAULT
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <features.h>
#include <string.h>

#include "libcr.h"

int main(void)
{
    cr_client_id_t (*my_cr_init)(void);
    void *self_handle = dlopen(NULL, RTLD_LAZY);
    void *libcr_handle;
    cr_client_id_t client_id;

    my_cr_init = dlsym(self_handle, "cr_init");
    if (my_cr_init != NULL) {
	fprintf(stderr, "cr_init found unexpectedly in 'self', before dlopen()\n");
	exit(1);
    }

    my_cr_init = dlsym(RTLD_DEFAULT, "cr_init");
    if (my_cr_init != NULL) {
	fprintf(stderr, "cr_init found unexpectedly in default search, before dlopen()\n");
	exit(1);
    }

    libcr_handle = dlopen("libcr.so", RTLD_NOW);
    if (libcr_handle == NULL) {
	fprintf(stderr, "dlopen(libcr.so) failed unexpectedly.  Bad LD_LIBRARY_PATH?\n");
	exit(1);
    }

    my_cr_init = dlsym(self_handle, "cr_init");
    if (my_cr_init != NULL) {
	fprintf(stderr, "cr_init found unexpectedly in 'self', after dlopen()\n");
	exit(1);
    }

    my_cr_init = dlsym(RTLD_DEFAULT, "cr_init");
    if (my_cr_init != NULL) {
	fprintf(stderr, "cr_init found unexpectedly in default search, after dlopen()\n");
	exit(1);
    }

    my_cr_init = dlsym(libcr_handle, "cr_init");
    if (my_cr_init == NULL) {
	fprintf(stderr, "cr_init not in dlopen()ed library\n");
	exit(1);
    }

    client_id = my_cr_init();
    if (client_id < 0) {
	fprintf(stderr, "cr_init() call failed\n");
	exit(1);
    }

    return 0;
}
