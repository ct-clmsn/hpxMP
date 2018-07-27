#include <omp.h>
#include "../../src/ompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
//#include "/home/tianyi/pkgs/repo/openmp/runtime/test/ompt/callback.h"
#include "task_schedule_callback.h"
int main (int argc, char *argv[])
{
    int nthreads;
//#pragma omp task
    printf("This task is not in parallel regin\n");
#pragma omp parallel num_threads(2)
    {
        omp_wait_id_t tid;
        tid = omp_get_thread_num();
#pragma omp task
        printf("This task is performed by thread [%lu]\n", tid);
//#pragma omp task
//        printf("This task2 is performed by thread [%lu]\n", tid);
        /*won't work without this line*/
        nthreads = omp_get_num_threads();
    }
    printf("This is not in parallel region\n");
}



