//  Copyright (c) 2013 Jeremy Kemp
//  Copyright (c) 2013 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "hpx_runtime.h"

double hpx_runtime::get_time() {
    return walltime.now();

}
int hpx_runtime::get_num_threads() {
    return num_threads;
}
/*
bool hpx_runtime::run_mtx(void*(*work_function)(int tid), int lock_id) {
    mutex_type::scoped_lock l(lock_list[lock_id]);
    return work_function(get_thread_num());
}*/
void hpx_runtime::lock(int lock_id) {
    //assert(lock_id < lock_list.size());
    lock_list[lock_id]->lock();

}

void hpx_runtime::unlock(int lock_id) {
    lock_list[lock_id]->unlock();
}

int hpx_runtime::new_mtx(){
    mutex_type *new_mutex = new mutex_type;
    lock_list.push_back(new_mutex);
    return lock_list.size() - 1;

}
void hpx_runtime::barrier_wait(){
    globalBarrier->wait();
}

int hpx_runtime::get_thread_num() {
    auto thread_id = hpx::threads::get_self_id();
    auto *data = reinterpret_cast<thread_data*>(
                    hpx::threads::get_thread_data(thread_id) );
    return data->thread_num;
}

int hpx_main() {
    assert(false);
    return 1;
}

void wait_for_startup(boost::mutex& mtx, boost::condition& cond, bool& running){
    cout << "HPX OpenMP runtime has started" << endl;
    // Let the main thread know that we're done.
    {
        boost::mutex::scoped_lock lk(mtx);
        running = true;
        cond.notify_all();
    }
}

void fini_runtime_worker(boost::mutex& mtx,
        boost::condition& cond, bool& running) {
    hpx::stop();
}

void fini_runtime() {
    cout << "Stopping HPX OpenMP runtime" << endl;

    boost::mutex mtx;
    boost::condition cond;
    bool running = false;

    hpx::applier::register_thread_nullary(
        HPX_STD_BIND(&fini_runtime_worker, 
            boost::ref(mtx), boost::ref(cond), boost::ref(running))
      , "fini_runtime_worker");
}

void hpx_runtime::init(int Nthreads) {
    mutex_type::scoped_lock l(init_mtx);

    if (hpx_initialized)
        return;
    if(Nthreads > 0)
        num_threads = Nthreads;
    else
        num_threads = hpx::threads::hardware_concurrency();

    globalBarrier = new barrier(num_threads);
    cout << "Starting HPX OpenMP runtime" << endl; 

    using namespace boost::assign;
    std::vector<std::string> cfg;
    cfg += "hpx.os_threads=" + boost::lexical_cast<std::string>(num_threads);
    cfg += "hpx.run_hpx_main!=0";

    char const* hpx_args_raw = getenv("OMP_HPX_ARGS");

    int argc;
    char ** argv;

    if (hpx_args_raw) { 
        std::string tmp(hpx_args_raw);

        std::vector<std::string> hpx_args;
        boost::algorithm::split(hpx_args, tmp,
            boost::algorithm::is_any_of(";"),
                boost::algorithm::token_compress_on);

        // FIXME: For correctness check for signed overflow.
        argc = hpx_args.size() + 2;
        argv = new char*[argc];

        // FIXME: Should we do escaping?    
        for (boost::uint64_t i = 0; i < hpx_args.size(); ++i) {
            cout << "arg[" << i << "]: " << hpx_args[i] << endl;
            argv[i + 1] = strdup(hpx_args[i].c_str());
        }
    } else {
        argc = 2;
        argv = new char*[argc];
    }
    argv[0] = const_cast<char*>("hpxMP");
    argv[argc - 1] = const_cast<char*>("-Ihpx.stacks.use_guard_pages=0");
    HPX_STD_FUNCTION<int(boost::program_options::variables_map& vm)> f;
    boost::program_options::options_description desc_cmdline; 

    boost::mutex local_mtx;
    boost::condition cond;
    bool running = false;

    hpx::start(f, desc_cmdline, argc, argv, cfg,
        HPX_STD_BIND(&wait_for_startup, 
            boost::ref(local_mtx), boost::ref(cond), boost::ref(running)));

    { // Wait for the thread to run.
        boost::mutex::scoped_lock lk(local_mtx);
        if (!running)
            cond.wait(lk);
    }
    atexit(fini_runtime);
    delete[] argv;
    hpx_initialized = true;
}

void task_setup(omp_task_func task_func, int thread_num, void *firstprivates, void *fp) {
    thread_data *data_struct = new thread_data;
    data_struct->thread_num = thread_num;
    auto thread_id = hpx::threads::get_self_id();
    hpx::threads::set_thread_data( thread_id, reinterpret_cast<size_t>(data_struct));
    task_func(firstprivates, fp);
}
void hpx_runtime::create_task(omp_task_func taskfunc, void *frame_pointer,
                 void *firstprivates, int may_delay,
                 int is_tied, int blocks_parent) {
    auto *data = reinterpret_cast<thread_data*>(
            hpx::threads::get_thread_data(hpx::threads::get_self_id()));
    int current_tid = data->thread_num;
    data->task_handles.push_back(hpx::async(task_setup, taskfunc, current_tid, firstprivates, frame_pointer));
}

void hpx_runtime::task_wait() {
    auto *data = reinterpret_cast<thread_data*>(
            hpx::threads::get_thread_data(hpx::threads::get_self_id()));
    hpx::wait(data->task_handles);
    data->task_handles.clear();

}
void ompc_fork_worker( int Nthreads, omp_task_func task_func, frame_pointer_t fp,
                       boost::mutex& mtx, boost::condition& cond, bool& running) {
    vector<hpx::lcos::future<void>> threads;
    threads.reserve(Nthreads);
    for(int i = 0; i < Nthreads; i++) {
        threads.push_back( hpx::async(task_setup, *task_func, i, (void*)0, fp));
    }
    hpx::lcos::wait(threads);
    // Let the main thread know that we're done.
    {
        boost::mutex::scoped_lock lk(mtx);
        running = true;
        cond.notify_all();
    }
}

void hpx_runtime::fork(int Nthreads, omp_task_func task_func, frame_pointer_t fp) { 
    boost::mutex mtx;
    boost::condition cond;
    bool running = false;

    hpx::applier::register_thread_nullary(
            HPX_STD_BIND(&ompc_fork_worker, Nthreads, task_func, fp,
                boost::ref(mtx), boost::ref(cond), boost::ref(running))
            , "ompc_fork_worker");
    // Wait for the thread to run.
    {
        boost::mutex::scoped_lock lk(mtx);
        if (!running)
            cond.wait(lk);
    }
}
