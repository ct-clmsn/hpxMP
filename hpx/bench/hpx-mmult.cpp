#include <hpx/hpx_init.hpp>
#include <hpx/runtime/threads/topology.hpp>
#include <boost/format.hpp>

#include <matrix_block.h>

using hpx::lcos::shared_future;
using hpx::lcos::future;
using std::vector;
using std::cout;
using std::endl;


const int blocksize = 2;

void print(block A) {
    for(int i = 0; i < A.height; i++) {
        for(int j = 0; j < A.width; j++) {
            cout << A[i][j] << " ";
        }
        cout << endl;
    }
    cout << endl;
}

block rec_mult(block A, block B, block C);

block serial_mult(block &A, block &B, block &C) {
    for (int i = 0; i < A.height; i++) {
        for (int j = 0; j < B.width; j++) {
            for (int k = 0; k < A.width;k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
    return C;
}

block add_blocks(block A, block B, block result) {
    for(int i = 0; i < A.height; i++){
        for(int j = 0; j < A.width; j++) {
            result[i][j] = A[i][j] + B[i][j];
        }
    }
    return result;
}

block calc_c11(block A, block B, block C) {
    block tempC(C.block11());//scratch space
    block A11B11 = rec_mult(A.block11(), B.block11(), C.block11());
    block A12B21 = rec_mult(A.block12(), B.block21(), tempC);
    return add_blocks(A11B11, A12B21, C.block11());
}

block calc_c12(block A, block B, block C) {
    block tempC(C.block12());
    block A11B12 = rec_mult(A.block11(), B.block12(), C.block12());
    block A12B22 = rec_mult(A.block12(), B.block22(), tempC);
    return add_blocks(A11B12, A12B22, C.block12());
}

block calc_c21(block A, block B, block C) {
    block tempC(C.block21());
    block A21B11 = rec_mult(A.block21(), B.block11(), C.block21());
    block A22B21 = rec_mult(A.block22(), B.block21(), tempC);
    return add_blocks(A21B11, A22B21, C.block21());
}

block calc_c22(block A, block B, block C) {
    block tempC(C.block22());
    block A21B12 = rec_mult(A.block21(), B.block12(), C.block22());
    block A22B22 = rec_mult(A.block22(), B.block22(), tempC);
    return add_blocks(A21B12, A22B22, C.block22());
}

block rec_mult(block A, block B, block C) {
    if(C.width <= blocksize || C.height <= blocksize ) {
        return serial_mult(A, B, C);
    } 
    block C11 = calc_c11(A, B, C);
    block C12 = calc_c12(A, B, C);
    block C21 = calc_c21(A, B, C);
    block C22 = calc_c22(A, B, C);

    return C;
}


int hpx_main(boost::program_options::variables_map& vm) {
    int niter = 1, N = blocksize * blocksize;
    //srand((unsigned long)time(NULL));
    srand(1);

    block a(N);
    block b(N);
    block c(new double[N*N], N);

    rec_mult(a, b, c);
    cout << " c (final)" << endl;
    print(c);

    return hpx::finalize();
}

int main(int argc, char ** argv) {

    hpx::init(argc, argv);

    return 0;
}
