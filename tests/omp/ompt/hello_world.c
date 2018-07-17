#include <omp.h>
#include <ompt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "callback.h"

int main (int argc, char *argv[])
{
    int nthreads;
    omp_state_t state;
#pragma omp parallel num_threads(2)
    {
        ompt_wait_id_t tid;
        tid = omp_get_thread_num();
        printf("Hello World from thread = [%lu]\n", tid);
        /*won't work without this line*/
        nthreads = omp_get_num_threads();
    }  /* All threads join master thread and disband */
    printf("this is not in parallel region\n");
}

