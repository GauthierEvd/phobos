#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"

#include <pthread.h>
#include <string.h>

static struct phobos_global_context *PHO_CONTEXT;
static __thread struct phobos_thread_context *PHO_THR_CONTEXT;
static pthread_mutex_t PHO_CONTEXT_LOCK = PTHREAD_MUTEX_INITIALIZER;

void _context_init(void)
{
    MUTEX_LOCK(&PHO_CONTEXT_LOCK);

    if (PHO_CONTEXT != NULL)
        goto unlock;

    PHO_CONTEXT = xcalloc(1, sizeof(*PHO_CONTEXT));

    pho_cfg_init_local(NULL);
    PHO_CONTEXT->log_level = PHO_LOG_DEFAULT;
    pho_log_callback_set(NULL); /* set default log callback */
    PHO_CONTEXT->log_dev_output = false;
    pthread_mutex_init(&PHO_CONTEXT->config.lock, NULL);
    pho_context_reset_mock_functions();

unlock:
    PHO_CONTEXT->pgc_refcount++;
    MUTEX_UNLOCK(&PHO_CONTEXT_LOCK);
}

/* must be called before calling any other phobos function */
int pho_context_init(void)
{
    int rc = 0;

    _context_init();

    if (PHO_THR_CONTEXT == NULL) {
        PHO_THR_CONTEXT = xcalloc(1, sizeof(*PHO_THR_CONTEXT));
        PHO_THR_CONTEXT->dss_handle = xmalloc(sizeof(
            *PHO_THR_CONTEXT->dss_handle));
        rc = dss_init(PHO_THR_CONTEXT->dss_handle);
    }

    return rc;
}

void pho_context_init_without_dss(void)
{
    _context_init();
}

void pho_context_fini(void)
{
    if (PHO_THR_CONTEXT != NULL) {
        if (PHO_THR_CONTEXT->dss_handle != NULL) {
            dss_fini(PHO_THR_CONTEXT->dss_handle);
            free(PHO_THR_CONTEXT->dss_handle);
        }
        free(PHO_THR_CONTEXT);
        PHO_THR_CONTEXT = NULL;
    }

    if (PHO_CONTEXT == NULL)
        return;

    MUTEX_LOCK(&PHO_CONTEXT_LOCK);

    PHO_CONTEXT->pgc_refcount--;

    if (PHO_CONTEXT->pgc_refcount == 0) {
        pho_cfg_local_fini();
        pthread_mutex_destroy(&PHO_CONTEXT->config.lock);
        free(PHO_CONTEXT);
        PHO_CONTEXT = NULL;
    }

    MUTEX_UNLOCK(&PHO_CONTEXT_LOCK);
}

struct phobos_global_context *phobos_context(void)
{
    return PHO_CONTEXT;
}

struct phobos_thread_context *phobos_thread_context(void)
{
    return PHO_THR_CONTEXT;
}

void phobos_module_context_set(struct phobos_global_context *context)
{
    PHO_CONTEXT = context;
}

void pho_context_reset_mock_functions(void)
{
    PHO_CONTEXT->mocks.mock_ioctl = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_mkdir = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_command_call = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_statfs = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_getxattr = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_setxattr = NULL;
    PHO_CONTEXT->mocks.mock_failure_after_second_partial_release = NULL;
}

struct dss_handle *pho_context_get_dss_handle(void)
{
    return PHO_THR_CONTEXT->dss_handle;
}
