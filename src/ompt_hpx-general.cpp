/*****************************************************************************
 * system include files
 ****************************************************************************/

#include <assert.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if KMP_OS_UNIX
#include <dlfcn.h>
#endif

/*****************************************************************************
 * ompt include files
 ****************************************************************************/
#include "ompt-internal.h"

/*****************************************************************************
 * macros
 ****************************************************************************/

#define ompt_get_callback_success 1
#define ompt_get_callback_failure 0

#define no_tool_present 0

#define OMPT_API_ROUTINE static

#ifndef OMPT_STR_MATCH
#define OMPT_STR_MATCH(haystack, needle) (!strcasecmp(haystack, needle))
#endif

/*****************************************************************************
 * types
 ****************************************************************************/

typedef struct {
    const char *state_name;
    omp_state_t state_id;
} omp_state_info_t;

typedef struct {
    const char *name;
    kmp_mutex_impl_t id;
} kmp_mutex_impl_info_t;

enum tool_setting_e {
    omp_tool_error,
    omp_tool_unset,
    omp_tool_disabled,
    omp_tool_enabled
};

/*****************************************************************************
 * global variables
 ****************************************************************************/

ompt_callbacks_active_t ompt_enabled;

omp_state_info_t omp_state_info[] = {
#define omp_state_macro(state, code) {#state, state},
        FOREACH_OMP_STATE(omp_state_macro)
#undef omp_state_macro
};

kmp_mutex_impl_info_t kmp_mutex_impl_info[] = {
#define kmp_mutex_impl_macro(name, id) {#name, name},
        FOREACH_KMP_MUTEX_IMPL(kmp_mutex_impl_macro)
#undef kmp_mutex_impl_macro
};

ompt_callbacks_internal_t ompt_callbacks;

static ompt_start_tool_result_t *ompt_start_tool_result = NULL;

/*****************************************************************************
 * forward declarations
 ****************************************************************************/

static ompt_interface_fn_t ompt_fn_lookup(const char *s);

OMPT_API_ROUTINE ompt_data_t *ompt_get_thread_data(void);


/*****************************************************************************
 * initialization and finalization (private operations)
 ****************************************************************************/
typedef ompt_start_tool_result_t *(*ompt_start_tool_t)(unsigned int,
                                                       const char *);

static ompt_start_tool_result_t *
ompt_try_start_tool(unsigned int omp_version, const char *runtime_version) {
    ompt_start_tool_result_t *ret = NULL;
    ompt_start_tool_t start_tool = NULL;
    ret = ompt_start_tool(omp_version, runtime_version);
    return ret;
}

void ompt_pre_init() {
    //--------------------------------------------------
    // Execute the pre-initialization logic only once.
    //--------------------------------------------------
    static int ompt_pre_initialized = 0;

    if (ompt_pre_initialized)
        return;

    ompt_pre_initialized = 1;

    //--------------------------------------------------
    // Use a tool iff a tool is enabled and available.
    //--------------------------------------------------
    const char *ompt_env_var = getenv("OMP_TOOL");
    tool_setting_e tool_setting = omp_tool_error;

    if (!ompt_env_var || !strcmp(ompt_env_var, ""))
        tool_setting = omp_tool_unset;
    else if (OMPT_STR_MATCH(ompt_env_var, "disabled"))
        tool_setting = omp_tool_disabled;
    else if (OMPT_STR_MATCH(ompt_env_var, "enabled"))
        tool_setting = omp_tool_enabled;

#if OMPT_DEBUG
    printf("ompt_pre_init(): tool_setting = %d\n", tool_setting);
#endif
    switch (tool_setting) {
        case omp_tool_disabled:
            break;

        case omp_tool_unset:
        case omp_tool_enabled:

            //--------------------------------------------------
            // Load tool iff specified in environment variable
            //--------------------------------------------------
            ompt_start_tool_result =
                    ompt_try_start_tool(1.0, "tianyi"); //TODO: version thing

            memset(&ompt_enabled, 0, sizeof(ompt_enabled));
            break;

        case omp_tool_error:
            fprintf(stderr, "Warning: OMP_TOOL has invalid value \"%s\".\n"
                            "  legal values are (NULL,\"\",\"disabled\","
                            "\"enabled\").\n",
                    ompt_env_var);
            break;
    }
#if OMPT_DEBUG
    printf("ompt_pre_init(): ompt_enabled = %d\n", ompt_enabled);
#endif
}