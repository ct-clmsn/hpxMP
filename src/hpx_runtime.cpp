//  Copyright (c) 2013 Jeremy Kemp
//  Copyright (c) 2013 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#define  HPX_LIMIT 9
#include "hpx_runtime.h"
#include "ompt-internal.h"
#include <functional>
#include <memory>

using std::cout;
using std::endl;

using hpx::dataflow;
using hpx::util::unwrapping;
using hpx::make_ready_future;
using hpx::threads::set_thread_data;
using hpx::threads::get_thread_data;
using hpx::threads::get_self_id;


extern std::shared_ptr<hpx_runtime> hpx_backend;


void wait_for_startup(std::mutex& startup_mtx, std::condition_variable& cond, bool& running)
{
    cout << "HPX OpenMP runtime has started" << endl;
    {   // Let the main thread know that we're done.
        //std::scoped_lock lk(startup_mtx);
        std::lock_guard<std::mutex> lk(startup_mtx);
        running = true;
        cond.notify_all();
    }
}

void fini_runtime()
{
    cout << "Stopping HPX OpenMP runtime" << endl;
    //this should only be done if this runtime started hpx
#if OMPT_SUPPORT
    if (ompt_enabled.ompt_callback_thread_end) {
        ompt_callbacks.ompt_callback(ompt_callback_thread_end)(
//                &(root->r.r_uber_thread->th.ompt_thread_info.thread_data));
                __ompt_get_thread_data_internal());
    }
#endif
    hpx::get_runtime().stop();
    cout << "Stopped" << endl;
}

void start_hpx(int initial_num_threads)
{
#ifdef OMP_COMPLIANT
    int num_hard_coded_args = 2;
#else
    int num_hard_coded_args = 1;
#endif
    std::vector<std::string> cfg;
    int argc;
    std::vector<char *> argv;
    using namespace boost::assign;
    cfg += "hpx.os_threads=" + boost::lexical_cast<std::string>(initial_num_threads);
    cfg += "hpx.stacks.use_guard_pages=0";
    cfg += "hpx.run_hpx_main!=0";
    //cfg += "hpx.stacks.huge_size=0x2000000";

    char const* hpx_args_raw = getenv("OMP_HPX_ARGS");

    std::vector<std::string> hpx_args;

    if (hpx_args_raw) {
        std::string tmp(hpx_args_raw);

        boost::algorithm::split(hpx_args, tmp,
            boost::algorithm::is_any_of(";"),
                boost::algorithm::token_compress_on);

        argc = hpx_args.size() + num_hard_coded_args;
        argv.resize(argc + 1);

        for (boost::uint64_t i = 0; i < hpx_args.size(); ++i) {
            argv[i + num_hard_coded_args] = const_cast<char*>(hpx_args[i].c_str());
        }
    } else {
        argc = num_hard_coded_args;
        argv.resize(argc + 1);
    }
    argv[0] = const_cast<char*>("hpxMP");
#ifdef OMP_COMPLIANT
    argv[1] = const_cast<char*>("--hpx:queuing=static");
#endif
    argv[argc] = nullptr;

    hpx::util::function_nonser<int(boost::program_options::variables_map& vm)> f;
    boost::program_options::options_description desc_cmdline;

    std::mutex startup_mtx;
    std::condition_variable cond;//TODO: replace this with something that can be checked later, once hpx is needed.
    bool running = false;

    hpx::start(f, desc_cmdline, argc, argv.data(), cfg,
            std::bind(&wait_for_startup, std::ref(startup_mtx), std::ref(cond), std::ref(running)));

    {
        //std::scoped_lock lk(startup_mtx);
        std::unique_lock<std::mutex> lk(startup_mtx);
        if (!running)
            cond.wait(lk);
    }
    atexit(fini_runtime);

//    delete[] argv;
}

hpx_runtime::hpx_runtime()
{
    int initial_num_threads;
    num_procs = hpx::threads::hardware_concurrency();
    char const* omp_num_threads = getenv("OMP_NUM_THREADS");

    if(omp_num_threads != NULL){
        initial_num_threads = atoi(omp_num_threads);
    } else {
        initial_num_threads = num_procs;
    }

    external_hpx = hpx::get_runtime_ptr();
    if(external_hpx){
        //It doesn't make much sense to try and use openMP thread settings
        // when the application has already initialized it's own threads.
        num_procs = hpx::get_os_thread_count();
        initial_num_threads = num_procs;
    }

    implicit_region.reset(new parallel_region(1));
    initial_thread.reset(new omp_task_data(implicit_region.get(), &device_icv, initial_num_threads));
    walltime.reset(new high_resolution_timer);

    if(!external_hpx) {
        start_hpx(initial_num_threads);
    }
}

parallel_region* hpx_runtime::get_team()
{
    auto task_data = get_task_data();
    auto team = task_data->team;
    return team;
}

omp_task_data* hpx_runtime::get_task_data()
{
    omp_task_data *data;
    if(hpx::threads::get_self_ptr()) {
        data = reinterpret_cast<omp_task_data*>(get_thread_data(get_self_id()));
        if(!data) {
            data = initial_thread.get();
        }
    } else {
        data = initial_thread.get();
    }
    return data;
}

double hpx_runtime::get_time() {
    return walltime->now();
}

int hpx_runtime::get_num_threads() {
    return get_team()->num_threads;
}

int hpx_runtime::get_num_procs() {
    return num_procs;
}

void hpx_runtime::set_num_threads(int nthreads) {
    if(nthreads > 0) {
        get_task_data()->icv.nthreads = nthreads;
        get_task_data()->threads_requested = nthreads;
    }
}

//TODO: Why not always return the given worker thread number?
int hpx_runtime::get_thread_num() {
//#ifdef OMP_COMPLIANT
    return hpx::get_worker_thread_num();
//#else
//    return get_task_data()->local_thread_num;
//#endif
}

// this should only be called from implicit tasks
void hpx_runtime::barrier_wait(){
    auto *team = get_team();
    task_wait();
#ifdef OMP_COMPLIANT
    while(team->exec->num_pending_closures() > 0 ) {
        //hpx::this_thread::sleep_for(std::chrono::milliseconds(100));
        hpx::this_thread::yield();
    }
#else
    int count = 1;
    int max_count = 100000;
    while(team->num_tasks > 0) {
        if(count == 1) {
            hpx::this_thread::yield();
        } else {
            int sleep_time = count;
            if(count > max_count)
                sleep_time = max_count;
            hpx::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
        }
        count = count * 2;
        //hpx::this_thread::yield();
    }
#endif
    if(team->num_threads > 1) {
        team->globalBarrier.wait();
    }
}

//TODO: Does the spec say that outstanding tasks need to end before this begins?
bool hpx_runtime::start_taskgroup()
{
    auto *task = get_task_data();
    task->in_taskgroup = true;
#ifdef OMP_COMPLIANT
    //FIXME: why is this local_thread_num? shouldn't it be team->num_threads
    //task->tg_exec.reset(new local_priority_queue_executor(task->local_thread_num));
    task->tg_exec.reset(new local_priority_queue_executor(task->team->num_threads));
#else
    task->tg_num_tasks.reset(new std::atomic<int64_t>{0});
#endif
    return true;
}

void hpx_runtime::end_taskgroup()
{
    auto *task = get_task_data();
#ifdef OMP_COMPLIANT
    task->tg_exec.reset();
#else
    while( *(task->tg_num_tasks) > 0 ) {
        //hpx::this_thread::sleep_for(std::chrono::milliseconds(100));
        hpx::this_thread::yield();
    }
    task->tg_num_tasks.reset();
#endif
    task->in_taskgroup = false;
}

void hpx_runtime::task_wait()
{
    auto *task = get_task_data();
    //TODO: Is this just an optimization? IT seems unnecessary.
    //if(task->df_map.size() > 0) {
    //    task->last_df_task.wait();
    //}
    //int count = 0;
    //int max_count = 10;
    while( *(task->num_child_tasks) > 0 ) {
        //int sleep_time = 10*count;
        //if(count > max_count)
        //    sleep_time = 10*max_count;
        //hpx::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        hpx::this_thread::yield();
    }
}

void task_setup( int gtid, kmp_task_t *task, omp_icv icv,
                 std::shared_ptr<std::atomic<int64_t>> parent_task_counter,
                 parallel_region *team)
{
    auto task_func = task->routine;
    omp_task_data task_data(gtid, team, icv);
    set_thread_data( get_self_id(), reinterpret_cast<size_t>(&task_data));

    task_func(gtid, task);

    *(parent_task_counter) -= 1;
#ifndef OMP_COMPLIANT
    team->num_tasks--;
#endif
    delete[] (char*)task;
}

#ifdef OMP_COMPLIANT
void tg_task_setup( int gtid, kmp_task_t *task, omp_icv icv,
                 std::shared_ptr<local_priority_queue_executor> tg_exec,
                 parallel_region *team)
{
    auto task_func = task->routine;
    omp_task_data task_data(gtid, team, icv);
    task_data.in_taskgroup = true;
    task_data.tg_exec = tg_exec;
    set_thread_data( get_self_id(), reinterpret_cast<size_t>(&task_data));

    task_func(gtid, task);

    delete[] (char*)task;
}
#endif

//shared_ptr is used for these counters, because the parent/calling task may terminate at any time,
//causing its omp_task_data to be deallocated.
void hpx_runtime::create_task( kmp_routine_entry_t task_func, int gtid, kmp_task_t *thunk)
{
    auto *current_task = get_task_data();

    if(current_task->team->num_threads > 1) {
#ifdef OMP_COMPLIANT
        if(current_task->in_taskgroup) {
            hpx::apply( *(current_task->tg_exec), tg_task_setup, gtid, thunk, current_task->icv,
                        current_task->tg_exec, current_task->team );
        } else {
            *(current_task->num_child_tasks) += 1;
            hpx::apply( *(current_task->team->exec), task_setup, gtid, thunk, current_task->icv,
                        current_task->num_child_tasks, current_task->team );
        }
#else
        //TODO: add taskgroups in non compliant version
        *(current_task->num_child_tasks) += 1;
        current_task->team->num_tasks++;
        hpx::apply(task_setup, gtid, thunk, current_task->icv,
                    current_task->num_child_tasks, current_task->team );
#endif
    } else {
        *(current_task->num_child_tasks) += 1;
        task_setup(gtid, thunk, current_task->icv, current_task->num_child_tasks, current_task->team);
    }
}

void df_task_wrapper( int gtid, kmp_task_t *task, omp_icv icv,
                      std::shared_ptr<std::atomic<int64_t>> task_counter,
                      parallel_region *team,
                      vector<shared_future<void>> deps)
{
    task_setup( gtid, task, icv, task_counter, team);
}

#ifdef OMP_COMPLIANT
void df_tg_task_wrapper( int gtid, kmp_task_t *task, omp_icv icv,
                        std::shared_ptr<local_priority_queue_executor> tg_exec,
                        parallel_region *team,
                        vector<shared_future<void>> deps)
{
    tg_task_setup( gtid, task, icv, tg_exec, team);
}
#endif


// The input on the Intel call is a pair of pointers to arrays of dep structs,
// and the length of these arrays.
// The structs contain a pointer and a flag for in or out dep
void hpx_runtime::create_df_task( int gtid, kmp_task_t *thunk,
                           int ndeps, kmp_depend_info_t *dep_list,
                           int ndeps_noalias, kmp_depend_info_t *noalias_dep_list )
{
    auto task = get_task_data();
    auto team = task->team;
    if(team->num_threads == 1 ) {
        create_task(thunk->routine, gtid, thunk);
    }
    vector<shared_future<void>> dep_futures;
    dep_futures.reserve( ndeps + ndeps_noalias);

    //Populating a vector of futures that the task depends on
    for(int i = 0; i < ndeps;i++) {
        if(task->df_map.count( dep_list[i].base_addr) > 0) {
            dep_futures.push_back(task->df_map[dep_list[i].base_addr]);
        }
    }
    for(int i = 0; i < ndeps_noalias;i++) {
        if(task->df_map.count( noalias_dep_list[i].base_addr) > 0) {
            dep_futures.push_back(task->df_map[noalias_dep_list[i].base_addr]);
        }
    }

    shared_future<void> new_task;

    if(task->in_taskgroup) {
    } else {
        *(task->num_child_tasks) += 1;
    }
#ifndef OMP_COMPLIANT
    team->num_tasks++;
#endif
    if(dep_futures.size() == 0) {
#ifdef OMP_COMPLIANT
        if(task->in_taskgroup) {
            new_task = hpx::async( *(task->tg_exec), tg_task_setup, gtid, thunk, task->icv,
                                    task->tg_exec, team);
        } else {
            new_task = hpx::async( *(team->exec), task_setup, gtid, thunk, task->icv,
                                    task->num_child_tasks, team);
        }
#else
        new_task = hpx::async( task_setup, gtid, thunk, task->icv,
                                task->num_child_tasks, team);
#endif
    } else {


#ifdef OMP_COMPLIANT
        //shared_future<shared_ptr<local_priority_queue_executor>> tg_exec = hpx::make_ready_future(task->tg_exec);

        if(task->in_taskgroup) {
            new_task = dataflow( *(task->tg_exec),
                                 unwrapping(df_tg_task_wrapper), gtid, thunk, task->icv,
                                 task->tg_exec,
                                 team, hpx::when_all(dep_futures) );
        } else {
            new_task = dataflow( *(team->exec),
                                 unwrapping(df_task_wrapper), gtid, thunk, task->icv,
                                 task->num_child_tasks,
                                 team, hpx::when_all(dep_futures) );
        }
#else
        new_task = dataflow( unwrapping(df_task_wrapper), gtid, thunk, task->icv,
                             task->num_child_tasks,
                             team, hpx::when_all(dep_futures) );
#endif
    }
    for(int i = 0 ; i < ndeps; i++) {
        if(dep_list[i].flags.out) {
            task->df_map[dep_list[i].base_addr] = new_task;
        }
    }
    for(int i = 0 ; i < ndeps_noalias; i++) {
        if(noalias_dep_list[i].flags.out) {
            task->df_map[noalias_dep_list[i].base_addr] = new_task;
        }
    }
    //task->last_df_task = new_task;
}

#ifdef FUTURIZE_TASKS
//This is for the unfinished compiler work for adding futures to OpenMP
raw_data future_wrapper( int gtid, kmp_task_t *task, raw_data arg1)
{
    memcpy( (task->shareds), arg1.data, arg1.size);

    task->routine(gtid, task);

    delete[] (char*)task;

    return arg1;
}

//I don't have access to which variable is output, assuming it's the first one for now.
raw_data future_wrapper2( int gtid, kmp_task_t *task, raw_data arg1, raw_data arg2)
{
    memcpy((task->shareds)            , arg1.data, arg1.size);
    memcpy((task->shareds) + arg1.size, arg2.data, arg2.size);

    task->routine(gtid, task);

    delete[] (char*)task;

    memcpy(arg1.data, (task->shareds), arg1.size);
    return arg1;
}

raw_data future_wrapper3( int gtid, kmp_task_t *task, raw_data arg1, raw_data arg2, raw_data arg3)
{
    memcpy((task->shareds),
            arg1.data, arg1.size);
    memcpy((task->shareds) + arg1.size,
            arg2.data, arg2.size);
    memcpy((task->shareds) + arg1.size + arg3.size,
            arg3.data, arg3.size);

    task->routine(gtid, task);

    delete[] (char*)task;

    memcpy(arg1.data, (task->shareds), arg1.size);
    return arg1;
}

void hpx_runtime::create_future_task( int gtid, kmp_task_t *thunk,
                                      int ndeps, kmp_depend_info_t *dep_list)
{
    shared_future<raw_data> *output_future;
    vector<shared_future<raw_data>*> input_futures(ndeps);

    //if the variables are FP, then the data needs to be copied, if it's shared, then only
    //pointers need to be set. working with the assumption/requirement that data is FP.
    for(int i=0; i < ndeps; i++) {
        input_futures[i] = (**(shared_future<raw_data>***)(dep_list[i].base_addr));
        if(dep_list[i].flags.out ) {
            output_future = (**(shared_future<raw_data>***)(dep_list[i].base_addr));
        }
    }

    if(ndeps == 1) {
        *(output_future) = dataflow( unwrapping(future_wrapper),
                                                make_ready_future(gtid), make_ready_future(thunk),
                                                *(input_futures[0]) );
    } else if(ndeps == 2) {
        *(output_future) = dataflow( unwrapping(future_wrapper2),
                                                make_ready_future(gtid), make_ready_future(thunk),
                                                *(input_futures[0]), *(input_futures[1]) );
    } else if(ndeps == 3) {
        *(output_future) = dataflow( unwrapping(future_wrapper3),
                                                make_ready_future(gtid), make_ready_future(thunk),
                                                *(input_futures[0]), *(input_futures[1]),
                                                *(input_futures[2]) );
    } else {
        cout << "too many dependencies for now" << endl;
    }
}
#endif

// --- start up for threads and parallel regions below --- //

void thread_setup( invoke_func kmp_invoke, microtask_t thread_func,
                   int argc, void **argv, int tid,
                   parallel_region *team, omp_task_data *parent,
                   mutex_type& barrier_mtx,
                   hpx::lcos::local::condition_variable_any& cond,
                   std::atomic<int>& running_threads )
{
    omp_task_data task_data(tid, team, parent);

    set_thread_data( get_self_id(), reinterpret_cast<size_t>(&task_data));

    if(argc == 0) { //note: kmp_invoke segfaults iff argc == 0
        thread_func(&tid, &tid);
    } else {
#if OMPT_SUPPORT
        //ompt_data_t *thread_data;
        //thread_local ompt_data_t thread_data;
    thread_local uint64_t id =  hpx_backend->get_thread_num();
    ompt_data[id].thread_data=ompt_data_none;
  if (ompt_enabled.enabled) {
//    thread_data = &(this_thr->th.ompt_thread_info.thread_data);
//    thread_data->ptr = NULL;
//
//    this_thr->th.ompt_thread_info.state = omp_state_overhead;
//    this_thr->th.ompt_thread_info.wait_id = 0;
//    this_thr->th.ompt_thread_info.idle_frame = OMPT_GET_FRAME_ADDRESS(0);
    if (ompt_enabled.ompt_callback_thread_begin) {
      ompt_callbacks.ompt_callback(ompt_callback_thread_begin)(
          ompt_thread_worker, __ompt_get_thread_data_internal());
    }
  }
#endif
#if OMPT_SUPPORT
//        void *dummy;
//        void **exit_runtime_p;
//        ompt_data_t *my_task_data;
//        ompt_data_t *my_parallel_data;
//        int ompt_team_size;
//
//        if (ompt_enabled.enabled) {
//            exit_runtime_p = &(
//                    team->t.t_implicit_task_taskdata[tid].ompt_task_info.frame.exit_frame);
//        } else {
//            exit_runtime_p = &dummy;
//        }
//
//        my_task_data =
//                &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data);
//        my_parallel_data = &(team->t.ompt_team_info.parallel_data);
//        if (ompt_enabled.ompt_callback_implicit_task) {
//            ompt_team_size = team->t.t_nproc;
//            ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
//                    ompt_scope_begin, my_parallel_data, my_task_data, ompt_team_size,
//                    __kmp_tid_from_gtid(gtid));
//            OMPT_CUR_TASK_INFO(this_thr)->thread_num = __kmp_tid_from_gtid(gtid);
//        }
        ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                ompt_scope_begin, 0, 0, 0,
                0);
#endif
        kmp_invoke(thread_func, tid, tid, argc, argv);
    }
    int count = 0;
    int max_count = 10;
    while (*(task_data.num_child_tasks) > 0 ) {
        if(count == 0) {
            hpx::this_thread::yield();
        } else {
            int sleep_time = 10*count;
            if(count > max_count)
                sleep_time = 10*max_count;
            hpx::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
        count++;
    }

    //This keeps the task_data on this stack allocated. When is that needed?
    //  if tasks are created without a barrier or taskwait, they could still
    //  reference their parents metadata(task_data above).
    //This combined with the waiting on child tasks above fufills the requirements
    //  of an OpenMP barrier.
    if(--running_threads == 0) {
        //hpx::lcos::local::spinlock::scoped_lock lk(barrier_mtx);
        std::unique_lock<mutex_type> lk(barrier_mtx);
        cond.notify_all();
    }
#if OMPT_SUPPORT
    if (ompt_enabled.ompt_callback_thread_end) {
//            std::cout<<"id="<<id<<std::endl;
        ompt_callbacks.ompt_callback(ompt_callback_thread_end)(__ompt_get_thread_data_internal());
    }
#endif
}

// This is the only place where get_thread can't be called, since
// that data is not initialized for the new hpx threads yet.
void fork_worker( invoke_func kmp_invoke, microtask_t thread_func,
                  int argc, void **argv,
                  omp_task_data *parent)
{
    parallel_region team(parent->team, parent->threads_requested);

#ifdef OMP_COMPLIANT
    team.exec.reset(new local_priority_queue_executor(parent->threads_requested));
#endif
    hpx::lcos::local::condition_variable_any  cond;
    mutex_type barrier_mtx;
    std::atomic<int> running_threads;
    running_threads = parent->threads_requested;

    for( int i = 0; i < parent->threads_requested; i++ ) {
        hpx::applier::register_thread_nullary(
                std::bind( &thread_setup, kmp_invoke, thread_func, argc, argv, i, &team, parent,
                           boost::ref(barrier_mtx), boost::ref(cond), boost::ref(running_threads) ),
                "omp_implicit_task", hpx::threads::pending,
                true, hpx::threads::thread_priority_low, i );
                //true, hpx::threads::thread_priority_normal, i );
    }
    {
        //hpx::lcos::local::spinlock::scoped_lock lk(barrier_mtx);
        std::unique_lock<mutex_type> lk(barrier_mtx);
        while( running_threads > 0 ) {
            cond.wait(lk);
        }
    }
    //The executor containing the tasks will be destroyed as this call goes out
    //of scope, which will wait on all tasks contained in it. So, nothing needs
    //to be done here for it.

    //I shouldn't need this. Tasks should be done before the thread exit.
    //FIXME: Remove this once the rest of the cond vars are in.
#ifndef OMP_COMPLIANT
    int count = 0;
    int max_count = 10;
    while(team.num_tasks > 0) {
        if(count == 0) {
            hpx::this_thread::yield();
        } else {
            int sleep_time = 10*count;
            if(count > max_count)
                sleep_time = 10*max_count;
            hpx::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
        count++;
    }
#endif
}

void fork_and_sync( invoke_func kmp_invoke, microtask_t thread_func,
                    int argc, void **argv,
                    omp_task_data *parent, std::mutex& fork_mtx,
                    std::condition_variable& cond, bool& running )
{
    fork_worker(kmp_invoke, thread_func, argc, argv, parent);
    {
        std::lock_guard<std::mutex> lk(fork_mtx);
        running = true;
        cond.notify_all();
    }
}

//TODO: This can make main an HPX high priority thread
//TODO: according to the spec, the current thread should be thread 0 of the new team, and execute the new work.
void hpx_runtime::fork(invoke_func kmp_invoke, microtask_t thread_func, int argc, void** argv)
{
    omp_task_data *current_task = get_task_data();
#if OMPT_SUPPORT
    ompt_invoker_t a;
    ompt_data_t *parent_task_data;
    __ompt_get_task_info_internal(0, NULL, &parent_task_data, NULL, NULL, NULL);
    ompt_data_t ompt_parallel_data=ompt_data_none;
    if (ompt_enabled.enabled) {
        ompt_thread_info_t* master_th = __ompt_get_thread_info_internal();
    }
    if (ompt_enabled.enabled) {
        if (ompt_enabled.ompt_callback_parallel_begin) {
            int team_size = current_task->icv.nthreads;
            ompt_callbacks.ompt_callback(ompt_callback_parallel_begin)(
                    parent_task_data, 0, &ompt_parallel_data, team_size,
                    a, 0);
        }
        uint64_t id = __ompt_get_thread_data_internal()->value;
      ompt_set_thread_state(&ompt_data[id],omp_state_overhead);
    }
#endif

    if( hpx::threads::get_self_ptr() ) {
        fork_worker(kmp_invoke, thread_func, argc, argv, current_task);
    } else {
        std::mutex fork_mtx;
        std::condition_variable cond;
        bool running = false;
        hpx::applier::register_thread_nullary(
                std::bind(&fork_and_sync,
                    kmp_invoke, thread_func, argc, argv,
                    current_task, boost::ref(fork_mtx), boost::ref(cond), boost::ref(running))
                , "ompc_fork_worker");
        {
            std::unique_lock<std::mutex> lk(fork_mtx);
            while (!running)
                cond.wait(lk);
        }
    }
    current_task->set_threads_requested(current_task->icv.nthreads );
#if OMPT_SUPPORT

//    *exit_runtime_p = NULL;
    if (ompt_enabled.enabled) {
//        OMPT_CUR_TASK_INFO(master_th)->frame.exit_frame = NULL;
        if (ompt_enabled.ompt_callback_implicit_task) {
            ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                    ompt_scope_end, NULL, 0, 1,
                   0);
        }
//        __ompt_lw_taskteam_unlink(master_th);

        if (ompt_enabled.ompt_callback_parallel_end) {
            ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
                    &ompt_parallel_data, __ompt_get_thread_data_internal(),a,
                    0);
        }
    }
#endif
}

