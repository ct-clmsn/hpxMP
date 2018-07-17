//
// Created by tianyi on 7/6/18.
//

#ifndef HPXMP_CALLBACK_HPP
#define HPXMP_CALLBACK_HPP

#include <stdio.h>
#include <inttypes.h>
#include <omp.h>
#include <ompt.h>


static const char* ompt_thread_type_t_values[] = {
        NULL,
        "ompt_thread_initial",
        "ompt_thread_worker",
        "ompt_thread_other"
};

static void format_task_type(int type, char *buffer) {
    char *progress = buffer;
    if (type & ompt_task_initial)
        progress += sprintf(progress, "ompt_task_initial");
    if (type & ompt_task_implicit)
        progress += sprintf(progress, "ompt_task_implicit");
    if (type & ompt_task_explicit)
        progress += sprintf(progress, "ompt_task_explicit");
    if (type & ompt_task_target)
        progress += sprintf(progress, "ompt_task_target");
    if (type & ompt_task_undeferred)
        progress += sprintf(progress, "|ompt_task_undeferred");
    if (type & ompt_task_untied)
        progress += sprintf(progress, "|ompt_task_untied");
    if (type & ompt_task_final)
        progress += sprintf(progress, "|ompt_task_final");
    if (type & ompt_task_mergeable)
        progress += sprintf(progress, "|ompt_task_mergeable");
    if (type & ompt_task_merged)
        progress += sprintf(progress, "|ompt_task_merged");
}

static ompt_set_callback_t ompt_set_callback;
static ompt_get_thread_data_t ompt_get_thread_data;
static ompt_get_parallel_info_t ompt_get_parallel_info;
static ompt_get_unique_id_t ompt_get_unique_id;

static void
on_ompt_callback_parallel_begin(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t* parallel_data,
        uint32_t requested_team_size,
        ompt_invoker_t invoker,
        const void *codeptr_ra)
{
    if(parallel_data->ptr)
        printf("0: parallel_data initially not null\n");
    parallel_data->value = ompt_get_unique_id();
    printf("%" PRIu64 ": ompt_event_parallel_begin: parent_task_id=%" PRIu64 ", parallel_id=%" PRIu64 ", requested_team_size=%" PRIu32 ",codeptr_ra=%p\n", ompt_get_thread_data()->value, encountering_task_data->value, parallel_data->value, requested_team_size,codeptr_ra);
}

static void
on_ompt_callback_parallel_end(
        ompt_data_t *parallel_data,
        ompt_data_t *encountering_task_data,
        ompt_invoker_t invoker,
        const void *codeptr_ra)
{
    printf("%" PRIu64 ": ompt_event_parallel_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, encountering_task_data->value);
}

static void
on_ompt_callback_task_create(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t* new_task_data,
        int type,
        int has_dependences,
        const void *codeptr_ra)
{
    if(new_task_data->ptr)
        printf("0: new_task_data initially not null\n");
    new_task_data->value = ompt_get_unique_id();
    char buffer[2048];

    format_task_type(type, buffer);

    //there is no parallel_begin callback for implicit parallel region
    //thus it is initialized in initial task
    if(type & ompt_task_initial)
    {
        ompt_data_t *parallel_data;
        ompt_get_parallel_info(0, &parallel_data, NULL);
        if(parallel_data->ptr)
            printf("%s\n", "0: parallel_data initially not null");
        parallel_data->value = ompt_get_unique_id();
    }

    printf("%" PRIu64 ": ompt_event_task_create: parent_task_id=%" PRIu64 ", parent_task_frame.exit=%p, parent_task_frame.reenter=%p, new_task_id=%" PRIu64 ", codeptr_ra=%p, task_type=%s=%d, has_dependences=%s\n", ompt_get_thread_data()->value, encountering_task_data ? encountering_task_data->value : 0, encountering_task_frame ? encountering_task_frame->exit_frame : NULL, encountering_task_frame ? encountering_task_frame->enter_frame : NULL, new_task_data->value, codeptr_ra, buffer, type, has_dependences ? "yes" : "no");
}

static void
on_ompt_callback_thread_begin(
        ompt_thread_type_t thread_type,
        ompt_data_t *thread_data)
{
    if(thread_data->ptr)
        printf("%s\n", "0: thread_data initially not null");
    thread_data->value = ompt_get_unique_id();
    printf("%" PRIu64 ": ompt_event_thread_begin: thread_type=%s=%d, thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, ompt_thread_type_t_values[thread_type], thread_type, thread_data->value);
}

static void
on_ompt_callback_thread_end(
        ompt_data_t *thread_data)
{
    printf("%" PRIu64 ": ompt_event_thread_end: thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data->value);
}

#define register_callback_t(name, type)                       \
              do{                                                           \
                type f_##name = &on_##name;                                 \
                if (ompt_set_callback(name, (ompt_callback_t)f_##name) ==   \
                    ompt_set_never)                                         \
                  printf("0: Could not register callback '" #name "'\n");   \
              }while(0)

#define register_callback(name) register_callback_t(name, name##_t)

int ompt_initialize(
        ompt_function_lookup_t lookup,
        ompt_data_t *tool_data)
{
    ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
    ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");
    ompt_get_parallel_info = (ompt_get_parallel_info_t) lookup("ompt_get_parallel_info");
    ompt_get_unique_id = (ompt_get_unique_id_t) lookup("ompt_get_unique_id");

    register_callback(ompt_callback_parallel_begin);
    register_callback(ompt_callback_parallel_end);
    register_callback(ompt_callback_task_create);

    ompt_callback_thread_begin_t fy_ompt_callback_thread_begin = &on_ompt_callback_thread_begin;
    ompt_set_callback(ompt_callback_thread_begin, (ompt_callback_t)fy_ompt_callback_thread_begin);
    register_callback(ompt_callback_thread_end);
    printf("0: NULL_POINTER=%p\n", (void*)NULL);
    return 1; //success
}

void ompt_finalize(ompt_data_t *tool_data)
{
    printf("0: ompt_event_runtime_shutdown\n");
}

ompt_start_tool_result_t* ompt_start_tool(
        unsigned int omp_version,
        const char *runtime_version)
{
    static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,&ompt_finalize, 0};
    return &ompt_start_tool_result;
}

#endif //HPXMP_CALLBACK_HPP
