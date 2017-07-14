// parallel edit of nanopond, from RNGs up

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#ifdef USE_SDL
#include <SDL.h>
#endif /* USE_SDL */
#include <omp.h>

// pond constants
#define POND_SIZE_X 640
#define POND_SIZE_Y 480
#define NUM_THREADS 8

// RNG variables; indexes and arrays
// RNG functions
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

static unsigned long rngArray[POND_SIZE_X * POND_SIZE_Y + 1][N];
static int rngIndexArray[POND_SIZE_X * POND_SIZE_Y + 1];

static void init_genrandArray(unsigned long s)
{
        int i, j;
        for (i = 0; i < POND_SIZE_X * POND_SIZE_Y + 1; i++) {
            rngArray[i][0] = (s + i) & 0xffffffffUL;
            for (j = 1; j < N; j++) {
                rngArray[i][j] = (1812433253UL * (rngArray[i][j-1] ^ (rngArray[i][j-1] >> 30)) + j);
                rngArray[i][j] &= 0xffffffffUL;
                    }      
        } 

        for (j = 0; j < POND_SIZE_X * POND_SIZE_Y + 1; j++) {
                    rngIndexArray[j] = N;
                        }
}

static inline uint32_t genrand_int32Array(int whichRNG) {
        uint32_t y;
            static uint32_t mag01[2]={0x0UL, MATRIX_A};
        if (rngIndexArray[whichRNG] >= N) { /* generate N words at one time */
            int kk;
            for (kk=0;kk<N-M;kk++) {
                y = (rngArray[whichRNG][kk]&UPPER_MASK)|(rngArray[whichRNG][kk+1]&LOWER_MASK);
                rngArray[whichRNG][kk] = rngArray[whichRNG][kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
             }
                    for (;kk<N-1;kk++) {
                                    y = (rngArray[whichRNG][kk]&UPPER_MASK)|(rngArray[whichRNG][kk+1]&LOWER_MASK);
                                                rngArray[whichRNG][kk] = rngArray[whichRNG][kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
                                                        }
                            y = (rngArray[whichRNG][N-1]&UPPER_MASK)|(rngArray[whichRNG][0]&LOWER_MASK);
                                    rngArray[whichRNG][N-1] = rngArray[whichRNG][M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];

                                            rngIndexArray[whichRNG] = 0;
                                                }
         
            y = rngArray[whichRNG][rngIndexArray[whichRNG]++];

            /* Tempering */
            y ^= (y >> 11);
            y ^= (y << 7) & 0x9d2c5680UL;
            y ^= (y << 15) & 0xefc60000UL;
            y ^= (y >> 18);

            return y;
}

static inline uintptr_t getRandomFromArray(int whichRNG)
{
    uintptr_t result;

    if (sizeof(uintptr_t) == 8) {
        return (uintptr_t)((((uint64_t)genrand_int32Array(whichRNG)) << 32) ^ ((uint64_t)genrand_int32Array(whichRNG)));
    } else {
        return (uintptr_t)genrand_int32Array(whichRNG);
    }
}

//array of locations where the threads can go to get the location of a random cell
unsigned long randomLocationX[NUM_THREADS];
unsigned long randomLocationY[NUM_THREADS];


//main
int main()  {
    uintptr_t i,j,x,y;

    //initializing rngs
    init_genrandArray(123);
    for (i = 0; i < N; i++) {
    printf("random number %lu seeded: %lu\n", i, rngArray[4][i]);
    }
    
    //for (i = 0; i < 1024; i++) {
         printf("random number %lu generated: %lu\n", i, getRandomFromArray(4));
    //}

//begin picking batch loop
    for (;;){

//picking batch of numbers. this loop generates N numbers, which will translate to N / 2 locations.
        for (i = 0; i < NUM_THREADS; i++) {
             //printf("random number %lu generated: %lu\n", i, getRandomFromArray(4));
            randomLocationX[i] = getRandomFromArray(4) % POND_SIZE_X;
            randomLocationY[i] = getRandomFromArray(4) % POND_SIZE_Y;
            printf("random location x: %lu y: %lu\n", randomLocationX[i], randomLocationY[i]);
        }


//begin parallel loop
//parallel section! doing smth with those numbers. executin cells
    #pragma omp parallel private(i)
        {
        #pragma omp for
        for (i = 0; i < NUM_THREADS; i++) {
            printf("on thread %d\n", omp_get_thread_num());
            int cellIndex;
            cellIndex = randomLocationX[i] + POND_SIZE_X * randomLocationY[i];
            printf("cellIndex %lu is %lu\n", i, cellIndex);
            printf("rng %lu printing rn %lu\n", cellIndex, getRandomFromArray(cellIndex));
        }
    }

//end parallel loop

//end picking batch loop
        exit(0);    //ends forever loop on first iteration
    }
}
