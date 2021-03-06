#include <stdio.h>

const int LOOPCOUNT = 10;

int j;
#pragma omp threadprivate(j)

int test_omp_single_copyprivate()                                   
{
    int result = 0;
    int nr_iterations = 0;
#pragma omp parallel
    {
        int i;
        for (i = 0; i < LOOPCOUNT; i++)
        {
#pragma omp single copyprivate(j)
            {
                nr_iterations++;
                j = i;
            }
#pragma omp critical
            {
                result = result + j - i;
            }
#pragma omp barrier
        }
    }
    if(!((result == 0) && (nr_iterations == LOOPCOUNT))){
        printf("result = %d, nr_iterations = %d\n",result, nr_iterations);
    }
    return ((result == 0) && (nr_iterations == LOOPCOUNT));
}
int main()
{
    if(test_omp_single_copyprivate())
        printf("result is correct\n");
    return 0;
}
