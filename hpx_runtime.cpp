//  Copyright (c) 2013 Jeremy Kemp
//  Copyright (c) 2013 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


#define  HPX_LIMIT 9

#include "hpx_runtime.h"

extern boost::shared_ptr<hpx_runtime> hpx_backend;

void wait_for_startup(boost::mutex& mtx, boost::condition& cond, bool& running){
    cout << "HPX OpenMP runtime has started" << endl;
    {   // Let the main thread know that we're done.
        boost::mutex::scoped_lock lk(mtx);
        running = true;
        cond.notify_all();
    }
}

void fini_runtime() {
    cout << "Stopping HPX OpenMP runtime" << endl;
    //this should only be done if this runtime started hpx
    hpx::get_runtime().stop();
}

        
void start_hpx(int initial_num_threads) {
    std::vector<std::string> cfg;
    int argc;
    char ** argv;
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

        // FIXME: For correctness check for signed overflow.
        argc = hpx_args.size() + 1;
        argv = new char*[argc];

        // FIXME: Should we do escaping?    
        for (boost::uint64_t i = 0; i < hpx_args.size(); ++i) {
            argv[i + 1] = const_cast<char*>(hpx_args[i].c_str());
        }
    } else {
        argc = 1;
        argv = new char*[argc];
    }
    argv[0] = const_cast<char*>("hpxMP");


    hpx::util::function_nonser<int(boost::program_options::variables_map& vm)> f;
    boost::program_options::options_description desc_cmdline; 

    boost::mutex local_mtx;
    boost::condition cond;//TODO: replace this with something that can be checked later, once hpx is needed.
    bool running = false;

    hpx::start(f, desc_cmdline, argc, argv, cfg,
            std::bind(&wait_for_startup, boost::ref(local_mtx), boost::ref(cond), boost::ref(running)));

    { 
        boost::mutex::scoped_lock lk(local_mtx);
        if (!running)
            cond.wait(lk);
    }
    
    atexit(fini_runtime);

    delete[] argv;
}

hpx_runtime::hpx_runtime() {
    int initial_num_threads;
    num_procs = hpx::threads::hardware_concurrency();
    char const* omp_num_threads = getenv("OMP_NUM_THREADS");

    if(omp_num_threads != NULL){
        initial_num_threads = atoi(omp_num_threads);
    } else { 
        initial_num_threads = num_procs;
    }
    //TODO:
    //OMP_NESTED -> initial_nest_var
    //cancel_var
    //stacksize_var
    /*
    char const* omp_max_levels = getenv("OMP_MAX_ACTIVE_LEVELS");
    if(omp_max_levels != NULL) { max_active_levels_var = atoi(omp_max_levels); }
    
    //Not device specific, so it needs to move to parallel region:
    char const* omp_thread_limit = getenv("OMP_THREAD_LIMIT");
    if(omp_thread_limit != NULL) { thread_limit_var = atoi(omp_thread_limit); }
    */

    external_hpx = hpx::get_runtime_ptr();
    if(external_hpx){
        //It doesn't make much sense to try and use openMP thread settings
        // when the application has already initialized it's own threads.
        num_procs = hpx::get_os_thread_count();
        initial_num_threads = num_procs;
    }

    //TODO: nthreads_var is a list of ints where the nth item corresponds
    // to the number of threads in nth level parallel regions.

    implicit_region.reset(new parallel_region(1));
    initial_thread.reset(new omp_task_data(implicit_region.get(), &device_icv, initial_num_threads));
    walltime.reset(new high_resolution_timer);

    if(!external_hpx) {
        start_hpx(initial_num_threads);
    }

    //char const* omp_stack_size = getenv("OMP_STACKSIZE");
}


//This isn't really a thread team, it's a region. I think.
parallel_region* hpx_runtime::get_team(){
    auto task_data = get_task_data();
    auto team = task_data->team;
    return team;
}

omp_task_data* hpx_runtime::get_task_data(){
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

int hpx_runtime::get_thread_num() {
    auto *data = get_task_data();
    return get_task_data()->thread_num;
}

// this should only be called from implicit tasks
void hpx_runtime::barrier_wait(){
    auto *team = get_team();
    auto *task = get_task_data();
    task_wait();
    while(*(task->num_thread_tasks) > 0){
        hpx::this_thread::yield();
    }
    if(team->num_threads > 1) {
        team->globalBarrier.wait();
    }
}

void hpx_runtime::task_wait() {
    auto *task = get_task_data();
    while( *(task->num_child_tasks) > 0 ) {
        hpx::this_thread::yield();
    }
}

void intel_task_setup( int gtid, kmp_task_t *task, omp_icv icv, 
                       shared_ptr<atomic<int>> parent_task_counter,
                       shared_ptr<atomic<int>> task_counter,
                       parallel_region *team) {
    auto task_func = task->routine;
    omp_task_data task_data(gtid, team, icv);
    set_thread_data( get_self_id(), reinterpret_cast<size_t>(&task_data));
    task_data.num_thread_tasks = task_counter;

    task_func(gtid, task);

    *(task_data.num_thread_tasks) -= 1;
    *(parent_task_counter) -= 1;
    delete[] (char*)task;
}

void hpx_runtime::create_intel_task( kmp_routine_entry_t task_func, int gtid, kmp_task_t *thunk){
    auto *current_task = get_task_data();
    *(current_task->num_child_tasks) += 1;

    if(current_task->num_taskgroup_tasks.use_count() > 0) {
        *(current_task->num_taskgroup_tasks) += 1;
        hpx::apply( intel_task_setup, gtid, thunk, current_task->icv, 
                    current_task->num_taskgroup_tasks, current_task->num_child_tasks,
                                    current_task->team );
    } else {
        *(current_task->num_thread_tasks) += 1;
        hpx::apply( intel_task_setup, gtid, thunk, current_task->icv,
                    current_task->num_thread_tasks, current_task->num_child_tasks,
                    current_task->team );
    }
}

// did I try changing the wrapper function to take a vector?
void df_sync_func(future<vector<shared_future<void>>> deps) {
    deps.wait();
}

void df_task_wrapper( int gtid, kmp_task_t *task, omp_icv icv, 
                       shared_ptr<atomic<int>> parent_task_counter,
                       shared_ptr<atomic<int>> task_counter,
                       parallel_region *team, vector<shared_future<void>> deps) {
intel_task_setup( gtid, task, icv, parent_task_counter, task_counter, team);
}

void hpx_runtime::create_df_task( int gtid, kmp_task_t *thunk, vector<int64_t> in_deps, vector<int64_t> out_deps) {
    auto task = get_task_data();
    vector<shared_future<void>> dep_futures;

    for(int i = 0; i < in_deps.size(); i++) {
        if(task->df_map.count( in_deps[i] ) > 0) {
            dep_futures.push_back(task->df_map[in_deps[i]]);
        }
    }
    for(int i = 0; i < out_deps.size(); i++) {
        if(task->df_map.count( out_deps[i] ) > 0) {
            dep_futures.push_back(task->df_map[out_deps[i]]);
        }
    }

    shared_future<void> new_task;

    if(task->num_taskgroup_tasks.use_count() > 0) {
        *(task->num_taskgroup_tasks) += 1;
    } else {
        *(task->num_thread_tasks) += 1;
    }
    *(task->num_child_tasks) += 1;
    if(dep_futures.size() == 0) {
        if(task->num_taskgroup_tasks.use_count() > 0) {
            new_task = hpx::async( intel_task_setup, gtid, thunk, task->icv, 
                    task->num_child_tasks, task->num_taskgroup_tasks, task->team);
        } else {
            new_task = hpx::async( intel_task_setup, gtid, thunk, task->icv,
                    task->num_child_tasks, task->num_thread_tasks, task->team);
        }
    } else {
        shared_future<kmp_task_t*>      f_thunk = make_ready_future( thunk );
        shared_future<int>              f_gtid  = make_ready_future( gtid );
        shared_future<omp_icv>          f_icv   = make_ready_future( task->icv );
        shared_future<parallel_region*> f_team  = make_ready_future( task->team );
        shared_future<shared_ptr<atomic<int>>> f_parent_counter  = hpx::make_ready_future( task->num_child_tasks);
        shared_future<shared_ptr<atomic<int>>> f_counter;
        if(task->num_taskgroup_tasks.use_count() > 0) {
            f_counter= hpx::make_ready_future( task->num_taskgroup_tasks );
        } else {
            f_counter= hpx::make_ready_future( task->num_thread_tasks );
        }
        //TODO: Is there a better way to do this?
        //shared_future<void> f1 = hpx::async(df_sync_func, hpx::when_all(dep_futures));

        //new_task = dataflow( unwrapped(intel_task_setup), f_gtid, f_thunk, 
        //                     f_icv, f_parent_counter, f_counter, f_team, f1);
        new_task = dataflow( unwrapped(df_task_wrapper), f_gtid, f_thunk, 
                             f_icv, f_parent_counter, f_counter, f_team,
                             hpx::when_all(dep_futures) );
    }
    for(int i = 0 ; i < out_deps.size(); i++) {
        task->df_map[out_deps[i]] = new_task;
    }
}

#ifdef BUILD_UH
void thread_setup( omp_micro thread_func, void *fp, int tid,
                   parallel_region *team, omp_task_data *parent ) {
#else
void thread_setup( invoke_func kmp_invoke, microtask_t thread_func, 
                   int argc, void **argv, int tid,
                   parallel_region *team, omp_task_data *parent ) {
#endif
    omp_task_data task_data(tid, team, parent);
    auto thread_id = get_self_id();
    set_thread_data( thread_id, reinterpret_cast<size_t>(&task_data));
#ifdef BUILD_UH
    thread_func(tid, fp);
#else
    if(argc == 0) { //note: kmp_invoke segfaults iff argc == 0
        thread_func(&tid, &tid);
    } else {
        kmp_invoke(thread_func, tid, tid, argc, argv);
    }
#endif
    while(*(task_data.num_thread_tasks) > 0) {
        hpx::this_thread::yield();
    }
}

//This is the only place where I can't call get_thread.
//That data is not initialized for the new hpx threads yet.
#ifdef BUILD_UH
void fork_worker( omp_micro thread_func, frame_pointer_t fp,
                  omp_task_data *parent) {
#else
void fork_worker( invoke_func kmp_invoke, microtask_t thread_func,
                  int argc, void **argv,
                  omp_task_data *parent) {
#endif

    parallel_region team(parent->team, parent->threads_requested);
    vector<hpx::lcos::future<void>> threads;

    for( int i = 0; i < parent->threads_requested; i++ ) {
#ifdef BUILD_UH
        threads.push_back( hpx::async( thread_setup, *thread_func, fp, i, &team, parent ) );
#else
        threads.push_back( hpx::async( thread_setup, kmp_invoke, thread_func, argc, argv, i, &team, parent ) );
#endif
    }
    hpx::wait_all(threads);
}

#ifdef BUILD_UH
void fork_and_sync( omp_micro thread_func, frame_pointer_t fp, 
                    omp_task_data *parent, boost::mutex& mtx, 
                    boost::condition& cond, bool& running ) {
    fork_worker(thread_func, fp, parent);
#else
void fork_and_sync( invoke_func kmp_invoke, microtask_t thread_func, 
                    int argc, void **argv,
                    omp_task_data *parent, boost::mutex& mtx, 
                    boost::condition& cond, bool& running ) {
    fork_worker(kmp_invoke, thread_func, argc, argv, parent);
#endif

    {
        boost::mutex::scoped_lock lk(mtx);
        running = true;
        cond.notify_all();
    }
}
 
//For Intel, the Nthreads isn't passed in, another function sets Nthreads, so Nthreads should be 0;
// Also for Intel, fp is not a frame pointer, but a pointer to a struct,
//TODO: according to the spec, the current thread should be thread 0 of the new team, and execute the new work.
#ifdef BUILD_UH
void hpx_runtime::fork(int Nthreads, omp_micro thread_func, frame_pointer_t fp)
{ 
    omp_task_data *current_task = get_task_data();
    current_task->set_threads_requested( Nthreads );
    if( hpx::threads::get_self_ptr() ) {
        fork_worker(thread_func, fp, current_task);
#else
void hpx_runtime::fork(invoke_func kmp_invoke, microtask_t thread_func, int argc, void** argv)
{ 
    omp_task_data *current_task = get_task_data();
    if( hpx::threads::get_self_ptr() ) {
        fork_worker(kmp_invoke, thread_func, argc, argv, current_task);
#endif
    } else {
        boost::mutex mtx;
        boost::condition cond;
        bool running = false;
        hpx::applier::register_thread_nullary(
                std::bind(&fork_and_sync,
#ifdef BUILD_UH
                    thread_func, fp, 
#else
                    kmp_invoke, thread_func, argc, argv,
#endif
                    current_task, boost::ref(mtx), boost::ref(cond), boost::ref(running))
                , "ompc_fork_worker");
        {   // Wait for the thread to run.
            boost::mutex::scoped_lock lk(mtx);
            while (!running)
                cond.wait(lk);
        }
    }
    current_task->set_threads_requested(current_task->icv.nthreads );
}

