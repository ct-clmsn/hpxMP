/*****************************************************************************
 * system include files
 ****************************************************************************/

#include <assert.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

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

void ompt_post_init() {
    //--------------------------------------------------
    // Execute the post-initialization logic only once.
    //--------------------------------------------------
    static int ompt_post_initialized = 0;

    if (ompt_post_initialized)
        return;

    ompt_post_initialized = 1;

    //--------------------------------------------------
    // Initialize the tool if so indicated.
    //--------------------------------------------------
    if (ompt_start_tool_result) {
        ompt_enabled.enabled = !!ompt_start_tool_result->initialize(
                ompt_fn_lookup, &(ompt_start_tool_result->tool_data));

        if (!ompt_enabled.enabled) {
            // tool not enabled, zero out the bitmap, and done
            memset(&ompt_enabled, 0, sizeof(ompt_enabled));
            return;
        }

//        ompt_thread_t *root_thread = ompt_get_thread();
//
//        ompt_set_thread_state(root_thread, omp_state_overhead);
//
//        if (ompt_enabled.ompt_callback_thread_begin) {
//            ompt_callbacks.ompt_callback(ompt_callback_thread_begin)(
//                    ompt_thread_initial, __ompt_get_thread_data_internal());
//        }
//        ompt_data_t *task_data;
//        __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL, NULL);
//        if (ompt_enabled.ompt_callback_task_create) {
//            ompt_callbacks.ompt_callback(ompt_callback_task_create)(
//                    NULL, NULL, task_data, ompt_task_initial, 0, NULL);
//        }
//
//        ompt_set_thread_state(root_thread, omp_state_work_serial);
    }
}

/*****************************************************************************
 * callbacks
 ****************************************************************************/

OMPT_API_ROUTINE int ompt_set_callback(ompt_callbacks_t which,
                                       ompt_callback_t callback) {
    std::cout<<"ompt_set_callback"<<std::endl;
    switch (which) {

#define ompt_event_macro(event_name, callback_type, event_id)                  \
  case event_name:                                                             \
    if (ompt_event_implementation_status(event_name)) {                        \
      ompt_callbacks.ompt_callback(event_name) = (callback_type)callback;      \
      ompt_enabled.event_name = (callback != 0);                               \
    }                                                                          \
    if (callback)                                                              \
      return ompt_event_implementation_status(event_name);                     \
    else                                                                       \
      return ompt_set_always;

        FOREACH_OMPT_EVENT(ompt_event_macro)

#undef ompt_event_macro

        default:
            return ompt_set_error;
    }
}

/*****************************************************************************
 * API inquiry for tool
 ****************************************************************************/

static ompt_interface_fn_t ompt_fn_lookup(const char *s) {

#define ompt_interface_fn(fn)                                                  \
  fn##_t fn##_f = fn;                                                          \
  if (strcmp(s, #fn) == 0)                                                     \
    return (ompt_interface_fn_t)fn##_f;

    FOREACH_OMPT_INQUIRY_FN(ompt_interface_fn)

    return (ompt_interface_fn_t)0;
}