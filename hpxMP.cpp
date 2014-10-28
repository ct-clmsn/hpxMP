//  Copyright (c) 2013 Jeremy Kemp
//  Copyright (c) 2013 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "hpxMP.h"
#include "loop_data.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <boost/shared_ptr.hpp>

using namespace std;
boost::shared_ptr<hpx_runtime> hpx_backend;
boost::shared_ptr<loop_data> loop_sched;

bool started = false;
int single_counter = 0;
int current_single_thread = -1;

//mtx_ptr single_mtx; 
//mtx_ptr crit_mtx ;
//mtx_ptr print_mtx;

//overwrites global in openmp
int __ompc_init_rtl(int num_threads) {
    return 0;
}

void start_backend(){
    if( !hpx::get_runtime_ptr() ) {
        hpx_backend.reset(new hpx_runtime());
    }
    loop_sched.reset(new loop_data);

    //single_mtx.reset(new mutex_type);
    //crit_mtx.reset(new mutex_type);
    //print_mtx.reset(new mutex_type);
}

void __ompc_fork(int nthreads, omp_micro micro_task, frame_pointer_t fp) {
    assert(nthreads >= 0);
    if(!hpx_backend) {
        start_backend();
    }
    assert(!started);//Nested parallelism is disabled
    started = true;
    hpx_backend->fork(nthreads, micro_task, fp);
    started = false;
}

int __ompc_can_fork() {
    return !started;
}

void __ompc_reduction(int gtid, omp_lock_t **lck){
    __ompc_critical(gtid, lck);
}

void __ompc_end_reduction(int gtid, omp_lock_t **lck){
    __ompc_end_critical(gtid, lck);
}

void __ompc_barrier() {
    __ompc_ebarrier();
}

void __ompc_ebarrier() {
    if(started)
        hpx_backend->barrier_wait();
}

int __ompc_get_num_threads(){
    if(started)
        return hpx_backend->get_num_threads();
    return 0;
}

int __ompc_master(int global_tid){
    //TODO: if master can be called from tasks, then this doesn't work.
    if(__ompc_get_local_thread_num() == 0) 
        return 1;
    return 0;
}

void __ompc_end_master(int global_tid){
}

int __ompc_single(int tid){
    parallel_region *team = hpx_backend->get_team();
    if(!started)
        return 1;
    int num_threads = __ompc_get_num_threads();
    team->single_mtx.lock();
    if(current_single_thread == -1 && single_counter == 0) {
        current_single_thread = tid;
        single_counter = 1 - num_threads;
    } else {
        single_counter++;
    }
    team->single_mtx.unlock();
    if(current_single_thread == tid) {
        return 1;
    }
    return 0;
}

void __ompc_end_single(int tid){
    parallel_region *team = hpx_backend->get_team();
    if(!started)
        return;
    team->single_mtx.lock();
    if(single_counter == 0) {
        current_single_thread = -1;
    }
    team->single_mtx.unlock();
}

int __ompc_task_will_defer(int may_delay){
    //in the OpenUH runtime, this also checks if a task limit has been reached
    //leaving that to hpx to decide
    return may_delay;
}

void __ompc_task_firstprivates_alloc(void **firstprivates, int size){
    *firstprivates = malloc(size);
}

void __ompc_task_firstprivates_free(void *firstprivates){
    free(firstprivates);
}

void __ompc_task_create( omp_task_func task_func, void *frame_pointer,
                         void *firstprivates, int may_delay,
                         int is_tied, int blocks_parent) {
    // blocks_parent keeps parent tasks from exiting when they have shared 
    // variables in nested tasks.
    if(may_delay == 0 || !started) {
       task_func(firstprivates, frame_pointer);
    } else {
        hpx_backend->create_task( task_func, frame_pointer, firstprivates,
                                  is_tied, blocks_parent);
    }
}

void __ompc_task_wait(){
    if(started)
        hpx_backend->task_wait();
}

void __ompc_task_exit(){
    if(started)
        hpx_backend->task_exit();
}

void __ompc_serialized_parallel(int global_tid) {
}
void __ompc_end_serialized_parallel(int global_tid) {
}
//Note: volatile was removed from all the omp_lock_t calls
// and const_cast can get rid of the volatile if needed
void __ompc_critical(int gtid, omp_lock_t **lck) {
    parallel_region *team = hpx_backend->get_team();
    if(!started)
        return;
    omp_lock_t* tmp_mtx = new omp_lock_t;
    if(*lck == NULL ) {
        team->crit_mtx.lock();
        if(*lck == NULL ){
            *lck = tmp_mtx;
        }
        team->crit_mtx.unlock();
    } 
    if(&(*tmp_mtx) != &(**lck)) {
        delete tmp_mtx;
    }
    (**lck).lock();
}

void __ompc_end_critical(int gtid, omp_lock_t **lck) {
    if(started)
        (**lck).unlock();
}

omp_int32 __ompc_get_thdprv( void *** thdprv_p, omp_int64 size, 
                             void *datap, omp_int32 global_tid) {
    cout << "unimplemented function called: __ompc_get_thdprv" << endl;
    return 1;
}
omp_int32 __ompc_copyin_thdprv(int num,...) {
    cout << "unimplemented function called: __ompc_copyin_thdprv" << endl;
    return 0;
}

omp_int32 __ompc_copyprivate( omp_int32 mpsp_status,
                              void *cppriv, 
                              void(*cp)(void* src, void* dst) ) {
    cout << "unimplemented function called: __ompc_copyprivate" << endl;
    return 0;
}

int __ompc_get_local_thread_num() {
    if(hpx_backend && started) {
        return hpx_backend->get_thread_num();
    }
    return 0;
}
//OpenMP 3.1 spec, section 3.2.1
#pragma weak omp_set_num_threads_ = omp_set_num_threads
void omp_set_num_threads(int nthreads){
    hpx_backend->set_num_threads(nthreads);
}

//OpenMP 3.1 spec, section 3.2.2
//can be called from outside a parallel region
#pragma weak omp_get_num_threads_ = omp_get_num_threads
int omp_get_num_threads() {
    if(hpx_backend && started)
        return hpx_backend->get_num_threads();
    return 1;
}

//OpenMP 3.1 spec, section 3.2.3
#pragma weak omp_get_max_threads_ = omp_get_max_threads
int omp_get_max_threads() {
    if(!hpx_backend) {
        start_backend();
    }                      
    return hpx_backend->get_num_threads();
}

//OpenMP 3.1 spec, section 3.2.4
#pragma weak omp_get_thread_num_ = omp_get_thread_num
int omp_get_thread_num() {
    return __ompc_get_local_thread_num();
}

//OpenMP 3.1 spec, section 3.2.5
#pragma weak omp_get_num_procs_ = omp_get_num_procs
int omp_get_num_procs() {
    if(!hpx_backend) {
        start_backend();
    }
    return hpx_backend->get_num_procs();
}

//OpenMP 3.1 spec, section 3.2.6
int omp_in_parallel(){
    return started;
}

//OpenMP 3.1 spec, section 3.2.7
void omp_set_dynamic(int dynamic_threads){
    //The omp_set_dynamic routine enables or disables dynamic adjustment of the
    //number of threads available for the execution of subsequent parallel regions by
    //setting the value of the dyn-var ICV.
}
//OpenMP 3.1 spec, section 3.2.8
//int omp_get_dynamic(void);
//OpenMP 3.1 spec, section 3.2.9
void omp_set_nested(int nested){
}
//OpenMP 3.1 spec, section 3.2.10
//int omp_get_nested(void);
//OpenMP 3.1 spec, section 3.2.11
//void omp_set_schedule(omp_sched_t kind, int modifier);
//OpenMP 3.1 spec, section 3.2.12
//void omp_get_schedule(omp_sched_t * kind, int * modifier );
//OpenMP 3.1 spec, section 3.2.13
//int omp_get_thread_limit(void);
//OpenMP 3.1 spec, section 3.2.14
//void omp_set_max_active_levels (int max_levels);
//OpenMP 3.1 spec, section 3.2.15
//int omp_get_max_active_levels(void);
//OpenMP 3.1 spec, section 3.2.16
//int omp_get_level (void);
//OpenMP 3.1 spec, section 3.2.17
//int omp_get_ancestor_thread_num (int level);
//OpenMP 3.1 spec, section 3.2.18
//int omp_get_team_size (int level);
//OpenMP 3.1 spec, section 3.2.19
//int omp_get_active_level (void);
//OpenMP 3.1 spec, section 3.2.20
//int omp_in_final(void);

//OpenMP 3.1 spec, section 3.3.1
void omp_init_lock(omp_lock_t *lock) {
    if(!hpx_backend) {
        start_backend();
    }
    lock = new omp_lock_t;
}

void omp_init_nest_lock(omp_nest_lock_t *lock) {
}

//OpenMP 3.1 spec, section 3.3.2
void omp_destroy_lock(omp_lock_t *lock) {
    delete lock;
}

void omp_destroy_nest_lock(omp_nest_lock_t *lock) {
}

//OpenMP 3.1 spec, section 3.3.3
void omp_set_lock(omp_lock_t *lock) {
    lock->lock();
}

void omp_set_nest_lock(omp_nest_lock_t *lock) {
}

//OpenMP 3.1 spec, section 3.3.4
void omp_unset_lock(omp_lock_t *lock) {
    lock->unlock();
}

void omp_unset_nest_lock(omp_nest_lock_t *lock) {
}

//OpenMP 3.1 spec, section 3.3.5
int omp_test_lock(omp_lock_t *lock) {
    if(lock->try_lock())
        return 1;
    return 0;
}

int omp_test_nest_lock(omp_nest_lock_t *lock) {
    return 1;
}

//OpenMP 3.1 spec, section 3.4.1
double omp_get_wtime() {
    return hpx_backend->get_time();
}

//OpenMP 3.1 spec, section 3.4.2
double omp_get_wtick() {
    //high resolution elapsed_min
    return .000000001;
}

