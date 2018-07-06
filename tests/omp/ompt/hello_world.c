#include <omp.h>
#include <ompt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "callback.h"

int main (int argc, char *argv[])
{
/* Fork a team of threads giving them their own copies of variables */
#pragma omp parallel num_threads(2)
    {
        omp_wait_id_t tid;
        /* Obtain thread number */
        tid = omp_get_thread_num();
        // state = ompt_get_state(tid);
        printf("Hello World from thread = [%lu]\n", tid);
    }  /* All threads join master thread and disband */
    printf("this is not in parallel region\n");
}

