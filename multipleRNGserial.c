// THIS VERSION USES MULTIPLE RANDOM GENERATORS, BUT IS STILL SERIAL.
// THE LAST INDEX OF THE ARRAY OF RNG ARRAYS IS USED TO PICK CELLS AND
// INTRODUCE RANDOM CELLS WITH ENERGY.
// THE REST ARE FOR EACH INDIVIDUAL CELL EXECUTION, ASSIGNED BY LOCATION.
// A REPRODUCIBLE RESULT WOULD BE A PARALLEL VERSION THAT BEHAVES THE EXACT
// SAME AS THIS ALTERED, SERIAL VERSION OF NANOPOND.

/* *********************************************************************** */
/*                                                                         */
/* NanocellArray version 2.0 -- A teeny tiny artificial life virtual machine    */
/* Copyright (C) 2005 Adam Ierymenko - http://www.greythumb.com/people/api */
/* Copyright (C) 2017 Barry Rountree - rountree@llnl.gov		   */	
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify    */
/* it under the terms of the GNU General Public License as published by    */
/* the Free Software Foundation; either version 2 of the License, or       */
/* (at your option) any later version.                                     */
/*                                                                         */
/* This program is distributed in the hope that it will be useful,         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/* GNU General Public License for more details.                            */
/*                                                                         */
/* You should have received a copy of the GNU General Public License       */
/* along with this program; if not, write to the Free Software             */
/* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110 USA     */
/*                                                                         */
/* *********************************************************************** */

/*
 * Changelog:
 *
 * 1.0 - Initial release
 * 1.1 - Made empty cells get initialized with 0xffff... instead of zeros
 *       when the simulation starts. This makes things more consistent with
 *       the way the output buf is treated for self-replication, though
 *       the initial state rapidly becomes irrelevant as the simulation
 *       gets going.  Also made one or two very minor performance fixes.
 * 1.2 - Added statistics for execution frequency and metabolism, and made
 *       the visualization use 16bpp color.
 * 1.3 - Added a few other statistics.
 * 1.4 - Replaced SET with KILL and changed EAT to SHARE. The SHARE idea
 *       was contributed by Christoph Groth (http://www.falma.de/). KILL
 *       is a variation on the original EAT that is easier for cells to
 *       make use of.
 * 1.5 - Made some other instruction changes such as XCHG and added a
 *       penalty for failed KILL attempts. Also made access permissions
 *       stochastic.
 * 1.6 - Made cells all start facing in direction 0. This removes a bit
 *       of artificiality and requires cells to evolve the ability to
 *       turn in various directions in order to reproduce in anything but
 *       a straight line. It also makes pretty graphics.
 * 1.7 - Added more statistics, such as original lineage, and made the
 *       genome report files CSV files as well.
 * 1.8 - Fixed LOOP/REP bug updateed by user Sotek.  Thanks!  Also
 *       reduced the default mutation rate a bit.
 * 1.9 - Added a bunch of changes suggested by Christoph Groth: a better
 *       coloring algorithm, right click to switch coloring schemes (two
 *       are currently supported), and a few speed optimizations. Also
 *       changed visualization so that cells with generations less than 2
 *       are no longer shown.
 * 2.0 - It has been over a decade since this tiny program was updated.
 * 	 Ported to SDL 2.0.5 and cleaned up the code to make it more 
 * 	 appropriate for optimization pedagogy.
 */

/*
 * NanocellArray is just what it says: a very very small and simple artificial
 * life virtual machine.
 *
 * It is a "small evolving program" based artificial life system of the same
 * general class as Tierra, Avida, and Archis.  It is written in very tight
 * and efficient C code to make it as fast as possible, and is so small that
 * it consists of only one .c file.
 *
 * How NanocellArray works:
 *
 * The NanocellArray world is called a "cellArray."  It is an NxM two dimensional
 * array of Cell structures, and it wraps at the edges (it's toroidal).
 * Each Cell structure consists of a few attributes that are there for
 * statistics purposes, an energy level, and an array of MAX_NUM_INSTR
 * four-bit values.  (The four-bit values are actually stored in an array
 * of machine-size words.)  The array in each cell contains the genome
 * associated with that cell, and MAX_NUM_INSTR is therefore the maximum
 * allowable size for a cell genome.
 *
 * The first four bit value in the genome is called the "logo." What that is
 * for will be explained later. The remaining four bit values each code for
 * one of 16 instructions. Instruction zero (0x0) is NOP (no operation) and
 * instruction 15 (0xf) is STOP (stop cell execution). Read the code to see
 * what the others are. The instructions are exceptionless and lack fragile
 * operands. This means that *any* arbitrary sequence of instructions will
 * always run and will always do *something*. This is called an evolvable
 * instruction set, because programs coded in an instruction set with these
 * basic characteristics can mutate. The instruction set is also
 * Turing-complete, which means that it can theoretically do anything any
 * computer can do. If you're curious, the instruciton set is based on this:
 * http://www.muppetlabs.com/~breadbox/bf/
 *
 * At the center of NanocellArray is a core loop. Each time this loop executes,
 * a clock counter is incremented and one or more things happen:
 *
 * - Every UPDATE_FREQUENCY clock ticks a line of comma seperated output
 *   is printed to STDOUT with some statistics about what's going on.
 * - Every REPORT_FREQUENCY clock ticks, all viable replicators (cells whose
 *   generation is >= 2) are reported to a file on disk.
 * - Every INFLOW_FREQUENCY clock ticks a random x,y location is picked,
 *   energy is added (see INFLOW_RATE_MEAN and INFLOW_RATE_DEVIATION)
 *   and it's genome is filled with completely random bits.  Statistics
 *   are also reset to generation==0 and parentID==0 and a new cell ID
 *   is assigned.
 * - Every tick a random x,y location is picked and the genome inside is
 *   executed until a STOP instruction is encountered or the cell's
 *   energy counter reaches zero. (Each instruction costs one unit energy.)
 *
 * The cell virtual machine is an extremely simple register machine with
 * a single four bit register, one memory pointer, one spare memory pointer
 * that can be exchanged with the main one, and an output buffer. When
 * cell execution starts, this output buffer is filled with all binary 1's
 * (0xffff....). When cell execution is finished, if the first byte of
 * this buffer is *not* 0xff, then the VM says "hey, it must have some
 * data!". This data is a candidate offspring; to reproduce cells must
 * copy their genome data into the output buffer.
 *
 * When the VM sees data in the output buffer, it looks at the cell
 * adjacent to the cell that just executed and checks whether or not
 * the cell has permission (see below) to modify it. If so, then the
 * contents of the output buffer replace the genome data in the
 * adjacent cell. Statistics are also updated: parentID is set to the
 * ID of the cell that generated the output and generation is set to
 * one plus the generation of the parent.
 *
 * A cell is permitted to access a neighboring cell if:
 *    - That cell's energy is zero
 *    - That cell's parentID is zero
 *    - That cell's logo (remember?) matches the trying cell's "guess"
 *
 * Since randomly introduced cells have a parentID of zero, this allows
 * real living cells to always replace them or eat them.
 *
 * The "guess" is merely the value of the register at the time that the
 * access attempt occurs.
 *
 * Permissions determine whether or not an offspring can take the place
 * of the contents of a cell and also whether or not the cell is allowed
 * to EAT (an instruction) the energy in it's neighbor.
 *
 * If you haven't realized it yet, this is why the final permission
 * criteria is comparison against what is called a "guess." In conjunction
 * with the ability to "eat" neighbors' energy, guess what this permits?
 *
 * Since this is an evolving system, there have to be mutations. The
 * MUTATION_RATE sets their probability. Mutations are random variations
 * with a frequency defined by the mutation rate to the state of the
 * virtual machine while cell genomes are executing. Since cells have
 * to actually make copies of themselves to replicate, this means that
 * these copies can vary if mutations have occurred to the state of the
 * VM while copying was in progress.
 *
 * What results from this simple set of rules is an evolutionary game of
 * "corewar." In the beginning, the process of randomly generating cells
 * will cause self-replicating viable cells to spontaneously emerge. This
 * is something I call "random genesis," and happens when some of the
 * random gak turns out to be a program able to copy itself. After this,
 * evolution by natural selection takes over. Since natural selection is
 * most certainly *not* random, things will start to get more and more
 * ordered and complex (in the functional sense). There are two commodities
 * that are scarce in the cellArray: space in the NxN grid and energy. Evolving
 * cells compete for access to both.
 *
 * If you want more implementation details such as the actual instruction
 * set, read the source. It's well commented and is not that hard to
 * read. Most of it's complexity comes from the fact that four-bit values
 * are packed into machine size words by bit shifting. Once you get that,
 * the rest is pretty simple.
 *
 * NanocellArray, for it's simplicity, manifests some really interesting
 * evolutionary dynamics. While I haven't run the kind of multiple-
 * month-long experiment necessary to really see this (I might!), it
 * would appear that evolution in the cellArray doesn't get "stuck" on just
 * one or a few forms the way some other simulators are apt to do.
 * I think simplicity is partly reponsible for this along with what
 * biologists call embeddedness, which means that the cells are a part
 * of their own world.
 *
 * Run it for a while... the results can be... interesting!
 *
 * Running NanocellArray:
 *
 * NanocellArray can use SDL2 (Simple Directmedia Layer) for screen output. If
 * you don't have SDL, comment out USE_SDL below and you'll just see text
 * statistics and get genome data reports. (Turning off SDL will also speed
 * things up slightly.)
 *
 * After looking over the tunable parameters below, compile NanocellArray and
 * run it. Here are some example compilation commands from Linux:
 *
 * For Pentiums:
 *  gcc -O6 -march=pentium -funroll-loops -fomit-frame-pointer -s
 *   -o nanocellArray nanocellArray.c -lSDL
 *
 * For Athlons with gcc 4.0+:
 *  gcc -O6 -msse -mmmx -march=athlon -mtune=athlon -ftree-vectorize
 *   -funroll-loops -fomit-frame-pointer -o nanocellArray nanocellArray.c -lSDL
 *
 * The second line is for gcc 4.0 or newer and makes use of GCC's new
 * tree vectorizing feature. This will speed things up a bit by
 * compiling a few of the loops into MMX/SSE instructions.
 *
 * This should also work on other Posix-compliant OSes with relatively
 * new C compilers. (Really old C compilers will probably not work.)
 * On other platforms, you're on your own! On Windows, you will probably
 * need to find and download SDL if you want pretty graphics and you
 * will need a compiler. MinGW and Borland's BCC32 are both free. I
 * would actually expect those to work better than Microsoft's compilers,
 * since MS tends to ignore C/C++ standards. If stdint.h isn't around,
 * you can fudge it like this:
 *
 * #define uintptr_t unsigned long (or whatever your machine size word is)
 * #define uint8_t unsigned char
 * #define uint16_t unsigned short
 * #define uint64_t unsigned long long (or whatever is your 64-bit int)
 *
 * When NanocellArray runs, comma-seperated stats (see doUpdate() for
 * the columns) are output to stdout and various messages are output
 * to stderr. For example, you might do:
 *
 * ./nanocellArray >>stats.csv 2>messages.txt &
 *
 * To get both in seperate files.
 *
 * <plug>
 * Be sure to visit http://www.greythumb.com/blog for your dose of
 * technobiology related news. Also, if you're ever in the Boston
 * area, visit http://www.greythumb.com/bostonalife to find out when
 * our next meeting is!
 * </plug>
 *
 * Have fun!
 */

/* ----------------------------------------------------------------------- */
/* Tunable parameters                                                      */
/* ----------------------------------------------------------------------- */

/* Time in seconds after which to stop at. Comment this out to run forever. */
//#define STOP_AT 100

/* Frequency of comprehensive updates-- lower values will provide more
 * info while slowing down the simulation. Higher values will give less
 * frequent updates. */
/* This is also the frequency of screen refreshes if SDL is enabled. */
#define UPDATE_FREQUENCY 100000

/* Frequency at which to report all viable replicators (generation > 2)
 * to a file named <clock>.report in the current directory.  Comment
 * out to disable. The cells are reported in hexadecimal, which is
 * semi-human-readable if you look at the big switch() statement
 * in the main loop to see what instruction is signified by each
 * four-bit value. */
  #define REPORT_FREQUENCY 10000000
//#define REPORT_FREQUENCY 100000000
//#define CYCLE_REPORT_FREQUENCY 10

/* Mutation rate -- range is from 0 (none) to 0xffffffff (all mutations!) */
/* To get it from a float probability from 0.0 to 1.0, multiply it by
 * 4294967295 (0xffffffff) and round. */
#define MUTATION_RATE 21475 /* p=~0.000005 */

/* How frequently should random cells / energy be introduced?
 * Making this too high makes things very chaotic. Making it too low
 * might not introduce enough energy. */
#define INFLOW_FREQUENCY 100

/* Base amount of energy to introduce per INFLOW_FREQUENCY ticks */
#define INFLOW_RATE_BASE 4000

/* A random amount of energy between 0 and this is added to
 * INFLOW_RATE_BASE when energy is introduced. Comment this out for
 * no variation in inflow rate. */
#define INFLOW_RATE_VARIATION 8000

/* Size of cellArray in X and Y dimensions. */
#define POND_SIZE_X 640
#define POND_SIZE_Y 480

/* Depth of cellArray in four-bit codons -- this is the maximum
 * genome size. This *must* be a multiple of 16! */
#define MAX_NUM_INSTR 512

/* This is the divisor that determines how much energy is taken
 * from cells when they try to KILL a viable cell neighbor and
 * fail. Higher numbers mean lower penalties. */
#define FAILED_KILL_PENALTY 2

/* Define this to use SDL. To use SDL, you must have SDL headers
 * available and you must link with the SDL library when you compile. */
/* Comment this out to compile without SDL visualization support. */
//#define USE_SDL 1

/*
 * To verify against original behavior.
*/
//#define SINGLE_RNG 1

/* ----------------------------------------------------------------------- */

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

/* ----------------------------------------------------------------------- */
/* This is the Mersenne Twister by Makoto Matsumoto and Takuji Nishimura   */
/* http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html  */
/* ----------------------------------------------------------------------- */

/* A few very minor changes were made by me - Adam */

/* 
   A C-program for MT19937, with initialization improved 2002/1/26.
   Coded by Takuji Nishimura and Makoto Matsumoto.

   Before using, initialize the state by using init_genrand(seed)  
   or init_by_array(init_key, key_length).

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.                          

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote 
        products derived from this software without specific prior written 
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


   Any feedback is very welcome.
   http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)
*/

/* Period parameters */  
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */
// array of RNG arrays, one for each thread, including cell picker thread
static unsigned long rngArray[POND_SIZE_X * POND_SIZE_Y + 1][N];
// array of RNG indices, one for each array in rngArray
static int rngIndexArray[POND_SIZE_X * POND_SIZE_Y + 1];

/* initializes mt[N] with a seed */
static void init_genrand(unsigned long s)
{
	mt[0]= s & 0xffffffffUL;
	for (mti=1; mti<N; mti++) {
	mt[mti] = (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti); 
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
        mt[mti] &= 0xffffffffUL;
        /* for >32 bit machines */
    }
}

/* initializes mt arrays in rngArray with a seed */
static void init_genrandArray(unsigned long s)
{
	int i, j;
	// setting values in each array, reusing j to keep track of which index is being processed
	for (i = 0; i < POND_SIZE_X * POND_SIZE_Y + 1; i++) {
		// fixed seed for testing
		//rngArray[i][0] = s & 0xffffffffUL;
		// Give each array a different seed, adding on its index to the original passed in seed
		rngArray[i][0] = (s + i) & 0xffffffffUL;
		for (j = 1; j < N; j++) {
			rngArray[i][j] = (1812433253UL * (rngArray[i][j-1] ^ (rngArray[i][j-1] >> 30)) + j);
          		/* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
         		/* In the previous versions, MSBs of the seed affect   */
          		/* only MSBs of the array mt[].                        */
          		/* 2002/01/09 modified by Makoto Matsumoto             */
          		rngArray[i][j] &= 0xffffffffUL;
          		/* for >32 bit machines */	
		}		
	}   

	// setting mti values in array of indices
	for (j = 0; j < POND_SIZE_X * POND_SIZE_Y + 1; j++) {
		rngIndexArray[j] = N;
	}

	//testing printfs to make sure original array and each of new arrays are same`
	//printf("mt[5]: %lu rngArray[0][5]: %lu\n", mt[5], rngArray[0][5]);
	//printf("mti[5]: %lu rngIndexArray[0][5]: %lu\n", mti[5], rngIndexArray[5]);

}

/* generates a random number on [0,0xffffffff]-interval */
static inline uint32_t genrand_int32() {
	uint32_t y;
	static uint32_t mag01[2]={0x0UL, MATRIX_A};
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

	if (mti >= N) { /* generate N words at one time */
		int kk;

		for (kk=0;kk<N-M;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
		}
		for (;kk<N-1;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
		}
		y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
		mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];

		mti = 0;
	}
  
	y = mt[mti++];

	/* Tempering */
	y ^= (y >> 11);
	y ^= (y << 7) & 0x9d2c5680UL;
	y ^= (y << 15) & 0xefc60000UL;
	y ^= (y >> 18);

	return y;
}

/* generates a random number on [0,0xffffffff]-interval */
static inline uint32_t genrand_int32Array(int whichRNG) {
	uint32_t y;
	static uint32_t mag01[2]={0x0UL, MATRIX_A};
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

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

/* ----------------------------------------------------------------------- */

/* Pond depth in machine-size words.  This is calculated from
 * MAX_NUM_INSTR and the size of the machine word. (The multiplication
 * by two is due to the fact that there are two four-bit values in
 * each eight-bit byte.) */
#define MAX_WORDS_GENOME (MAX_NUM_INSTR / (sizeof(uintptr_t) * 2))

/* Number of bits in a machine-size word */
#define BITS_IN_WORD (sizeof(uintptr_t) * 8)

/* Constants representing neighbors in the 2D grid. */
#define N_LEFT 0
#define N_RIGHT 1
#define N_UP 2
#define N_DOWN 3

/* Word and bit at which to start execution */
/* This is after the "logo" */
#define EXEC_START_WORD 0
#define EXEC_START_BIT 4

/* Number of bits set in binary numbers 0000 through 1111 */
static const uintptr_t BITS_IN_FOURBIT_WORD[16] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

/**
 * Structure for a cell in the cellArray
 */
struct Cell {
	uint64_t ID;			/* Globally unique cell ID */
	uint64_t parentID;		/* ID of the cell's parent */
	uint64_t lineage;		/* Equal to the cell ID of the first cell in the line. */
	uintptr_t generation;		/* Generations start at 0 and are incremented from there. */
	uintptr_t energy;		/* Energy level of this cell */
	uintptr_t genome[MAX_WORDS_GENOME];/* four-bit instructions packed into machine-size words */
};

struct Cell cellArray[POND_SIZE_X][POND_SIZE_Y];/* The cellArray is a 2D array of cells */

/* Currently selected color scheme */
enum { KINSHIP,LINEAGE,MAX_COLOR_SCHEME } colorScheme = KINSHIP;
const char *colorSchemeName[2] = { "KINSHIP", "LINEAGE" };

/**
 * Get a random number - for 64 bit machines; shifts and adds 2 32 bit numbers
 *
 * @return Random number
 */
static inline uintptr_t getRandom()
{
	uintptr_t result;
	/* A good optimizing compiler should optimize out this if */
	/* This is to make it work on 64-bit boxes */
	if (sizeof(uintptr_t) == 8)
		//return (uintptr_t)((((uint64_t)genrand_int32()) << 32) ^ ((uint64_t)genrand_int32()));
		result = (uintptr_t)((((uint64_t)genrand_int32()) << 32) ^ ((uint64_t)genrand_int32()));
	// For regular 32 bit boxes
	else 
		//return (uintptr_t)genrand_int32();
		result = (uintptr_t)genrand_int32();
	//printf("random %lu drawn\n", result);
	return result;
}

/**
 * Get a random number - for 64 bit machines; shifts and adds 2 32 bit numbers
 *
 * @return Random number
 */
static inline uintptr_t getRandomFromArray(int whichRNG)
{

	// for testing
	uintptr_t result;

	/* A good optimizing compiler should optimize out this if */
	/* This is to make it work on 64-bit boxes */
	if (sizeof(uintptr_t) == 8) {
		//for testing
		result = (uintptr_t)((((uint64_t)genrand_int32Array(whichRNG)) << 32) ^ ((uint64_t)genrand_int32Array(whichRNG)));
		//printf("rng %d spit out num %d on index %d  with 1st rand num in array %d\n", whichRNG, result, rngIndexArray[whichRNG], rngArray[whichRNG][0]);
		
		//return (uintptr_t)((((uint64_t)genrand_int32Array(whichRNG)) << 32) ^ ((uint64_t)genrand_int32Array(whichRNG)));
	// For regular 32 bit boxes
	} else { 
		// for testing
		result = (uintptr_t)genrand_int32Array(whichRNG);
		//printf("rng %d spit out num %d on index %d  with 1st rand num in array %d\n", whichRNG, result, rngIndexArray[whichRNG], rngArray[whichRNG][0]);
		
		//return (uintptr_t)genrand_int32Array(whichRNG);
	}
	//for testing
//	printf("random %lu drawn\n", result);
	return result;
}

/**
 * Structure for keeping some running tally type statistics
 */
struct PerUpdateStatCounters
{
	double instructionExecutions[16];/* Per-instruction-type execution count since last update. */
	double cellExecutions;		/* Number of cells executed since last update */
	uintptr_t viableCellsReplaced;	/* Number of viable cells replaced by other cells' offspring */
	uintptr_t viableCellsKilled;	/* Number of viable cells KILLed */
	uintptr_t viableCellShares;	/* Number of successful SHARE operations */
};


struct PerUpdateStatCounters statCounters;	/* Global statistics counters */

/**
 * Output a line of comma-seperated statistics data
 */
static void doUpdate(const uint64_t clock)
{
	static uint64_t lastTotalViableReplicators = 0;

	uintptr_t x,y;

	uint64_t totalActiveCells = 0;
	uint64_t totalEnergy = 0;
	uint64_t totalViableReplicators = 0;
	uintptr_t maxGeneration = 0;
  
	for(x=0;x<POND_SIZE_X;++x) {
		for(y=0;y<POND_SIZE_Y;++y) {
			struct Cell *const c = &cellArray[x][y];
			if (c->energy) {
				++totalActiveCells;
				totalEnergy += (uint64_t)c->energy;
				if (c->generation > 2)
					++totalViableReplicators;
				if (c->generation > maxGeneration)
					maxGeneration = c->generation;
			}
		}
	}
  
	/* Look here to get the columns in the CSV output */
	/* The first five are here and are self-explanatory */
	printf("%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
		(uint64_t)clock,
		(uint64_t)totalEnergy,
		(uint64_t)totalActiveCells,
		(uint64_t)totalViableReplicators,
		(uint64_t)maxGeneration,
		(uint64_t)statCounters.viableCellsReplaced,
		(uint64_t)statCounters.viableCellsKilled,
		(uint64_t)statCounters.viableCellShares
	);
  
	/* The next 16 are the average frequencies of execution for each instruction per cell execution. */
	double totalMetabolism = 0.0;
	for(x=0;x<16;++x) {
		totalMetabolism += statCounters.instructionExecutions[x];
		printf(",%.4f",
			(statCounters.cellExecutions > 0.0) 
			? (statCounters.instructionExecutions[x] / statCounters.cellExecutions) 
			: 0.0);
	}
  
	/* The last column is the average metabolism per cell execution */
	printf(",%.4f\n",
			(statCounters.cellExecutions > 0.0) 
			? (totalMetabolism / statCounters.cellExecutions) 
			: 0.0);
	fflush(stdout);
  
	if ((lastTotalViableReplicators > 0)&&(totalViableReplicators == 0))
		fprintf(stderr,
			"[EVENT] Viable replicators have gone extinct. "\
			"Please reserve a moment of silence.\n");
	else 
		if ((lastTotalViableReplicators == 0)&&(totalViableReplicators > 0))
			fprintf(stderr,"[EVENT] Viable replicators have appeared!\n");
  
	lastTotalViableReplicators = totalViableReplicators;
  
	/* Reset per-update stat counters */
	for(x=0;x<sizeof(statCounters);++x)
		((uint8_t *)&statCounters)[x] = (uint8_t)0;
}

/**
 * Reports all viable (generation > 2) cells to a file called <clock>.report
 */
static void doReport(const uint64_t clock)
{
	char buf[MAX_NUM_INSTR*2];
	FILE *d;
	uintptr_t x,y,wordPtr,shiftPtr,inst,stopCount,i;
	struct Cell *currCell;
  
	sprintf(buf,"r%lu.report.csv",clock);
	d = fopen(buf,"w");
	if (!d) {
		fprintf(stderr,"[WARNING] Could not open %s for writing.\n",buf);
		return;
	}
  
	fprintf(stderr,"[INFO] Reporting viable cells to %s\n",buf);
  
	for(x=0;x<POND_SIZE_X;++x) {
		for(y=0;y<POND_SIZE_Y;++y) {
			currCell = &cellArray[x][y];
			if (currCell->energy&&(currCell->generation > 2)) {
				fprintf(d,"ID: %lu, parent ID: %lu, lineage: %lu, generation: %lu\n",
					(uint64_t)currCell->ID,
					(uint64_t)currCell->parentID,
					(uint64_t)currCell->lineage,
					(uint64_t)currCell->generation);
				wordPtr = 0;
				shiftPtr = 0;
				stopCount = 0;
				for(i=0;i<MAX_NUM_INSTR;++i) {
					inst = (currCell->genome[wordPtr] >> shiftPtr) & 0xf;
					/* Four STOP instructions in a row is considered the end.
					* The probability of this being wrong is *very* small, and
					* could only occur if you had four STOPs in a row inside
					* a LOOP/REP pair that's always false. In any case, this
					* would always result in our *underestimating* the size of
					* the genome and would never result in an overestimation. */
					fprintf(d,"%lx",inst);
					if (inst == 0xf) { /* STOP */
						if (++stopCount >= 4)
							break;
					} else 
						stopCount = 0;
					if ((shiftPtr += 4) >= BITS_IN_WORD) {
						if (++wordPtr >= MAX_WORDS_GENOME) {
							wordPtr = 0;
							shiftPtr = 4;
						} else 
							shiftPtr = 0;
					}
				}
				fwrite("\n",1,1,d);
			}
		}
	}
	fclose(d);
}


/**
 * Reports all viable (generation > 2) cells to a file called <clock>.report
 */
static void doCycleReport(const uint64_t clock)
{
	char buf[MAX_NUM_INSTR*2];
	FILE *d;
	uintptr_t x,y,wordPtr,shiftPtr,inst,stopCount,i;
	struct Cell *currCell;
  
	sprintf(buf,"o%lu.cycleReport.csv",clock);
	d = fopen(buf,"w");
	if (!d) {
		fprintf(stderr,"[WARNING] Could not open %s for writing.\n",buf);
		return;
	}
  
	fprintf(stderr,"[INFO] Reporting viable cells to %s\n",buf);
  
	for(x=0;x<POND_SIZE_X;++x) {
		for(y=0;y<POND_SIZE_Y;++y) {
			currCell = &cellArray[x][y];
			if (currCell->energy&&(currCell->generation > 2)) {
				fprintf(d,"ID: %lu, parent ID: %lu, lineage: %lu, generation: %lu\n",
					(uint64_t)currCell->ID,
					(uint64_t)currCell->parentID,
					(uint64_t)currCell->lineage,
					(uint64_t)currCell->generation);
				wordPtr = 0;
				shiftPtr = 0;
				stopCount = 0;
				for(i=0;i<MAX_NUM_INSTR;++i) {
					inst = (currCell->genome[wordPtr] >> shiftPtr) & 0xf;
					/* Four STOP instructions in a row is considered the end.
					* The probability of this being wrong is *very* small, and
					* could only occur if you had four STOPs in a row inside
					* a LOOP/REP pair that's always false. In any case, this
					* would always result in our *underestimating* the size of
					* the genome and would never result in an overestimation. */
					fprintf(d,"%lx",inst);
					if (inst == 0xf) { /* STOP */
						if (++stopCount >= 4)
							break;
					} else 
						stopCount = 0;
					if ((shiftPtr += 4) >= BITS_IN_WORD) {
						if (++wordPtr >= MAX_WORDS_GENOME) {
							wordPtr = 0;
							shiftPtr = 4;
						} else 
							shiftPtr = 0;
					}
				}
				fwrite("\n",1,1,d);
			}
		}
	}
	fclose(d);
}


/**
 * Reports the genome of a cell to a file.
 */
#ifdef USE_SDL
static void reportCell(FILE *file, struct Cell *cell) {
	uintptr_t wordPtr,shiftPtr,inst,stopCount,i;

	if (cell->energy&&(cell->generation > 2)) {
		wordPtr = 0;
		shiftPtr = 0;
		stopCount = 0;
		for(i=0;i<MAX_NUM_INSTR;++i) {
			inst = (cell->genome[wordPtr] >> shiftPtr) & 0xf;
			/* Four STOP instructions in a row is considered the end.
			* The probability of this being wrong is *very* small, and
			* could only occur if you had four STOPs in a row inside
			* a LOOP/REP pair that's always false. In any case, this
			* would always result in our *underestimating* the size of
			* the genome and would never result in an overestimation. */
			fprintf(file,"%lx",inst);
			if (inst == 0xf) { /* STOP */
				if (++stopCount >= 4)
					break;
			} else 
				stopCount = 0;
			if ((shiftPtr += 4) >= BITS_IN_WORD) {
				if (++wordPtr >= MAX_WORDS_GENOME) {
					wordPtr = EXEC_START_WORD;
					shiftPtr = EXEC_START_BIT;
				} else 
					shiftPtr = 0;
			}
		}
	}
	fprintf(file,"\n");
}
#endif

/**
 * Get a neighbor in the cellArray
 */
static inline struct Cell *getNeighbor(const uintptr_t x,const uintptr_t y,const uintptr_t dir)
{
	/* Space is toroidal; it wraps at edges */
	switch(dir) {
		case N_LEFT: 	return (x) ? &cellArray[x-1][y] : &cellArray[POND_SIZE_X-1][y];
		case N_RIGHT: 	return (x < (POND_SIZE_X-1)) ? &cellArray[x+1][y] : &cellArray[0][y];
		case N_UP: 	return (y) ? &cellArray[x][y-1] : &cellArray[x][POND_SIZE_Y-1];
		case N_DOWN: 	return (y < (POND_SIZE_Y-1)) ? &cellArray[x][y+1] : &cellArray[x][0];
	}
	return &cellArray[x][y]; /* This should never be reached */
}

/**
 * Determines if c1 is allowed to access c2
 */
static inline int accessAllowed(struct Cell *const c2,const uintptr_t c1guess,int sense, int currRNG)
{
	/* Access permission is more probable if they are more similar in sense 0,
	* and more probable if they are different in sense 1. Sense 0 is used for
	* "negative" interactions and sense 1 for "positive" ones. */
	return sense 
#ifdef SINGLE_RNG
		? (((getRandom() & 0xf) >= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID)) 
		: (((getRandom() & 0xf) <= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID));
#else
		? (((getRandomFromArray(currRNG) & 0xf) >= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID)) 
		: (((getRandomFromArray(currRNG) & 0xf) <= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID));
#endif
}

/**
 * Gets the color that a cell should be
 */
static inline uint32_t getColor(struct Cell *c)
{
	// This used to be an 8-bit color.  Changing to 32-bits:  0xAARRGGBB
	uintptr_t i,j,word,sum,opcode,skipnext;

	if (c->energy) {
		switch(colorScheme) {
			case KINSHIP:
			/*
			* Kinship color scheme by Christoph Groth
			*
			* For cells of generation > 1, saturation and value are set to maximum.
			* Hue is a hash-value with the property that related genomes will have
			* similar hue (but of course, as this is a hash function, totally
			* different genomes can also have a similar or even the same hue).
			* Therefore the difference in hue should to some extent reflect the grade
			* of "kinship" of two cells.
			*/
				if (c->generation > 1) {
					sum = 0;
					skipnext = 0;
					for(i=0;
					i<MAX_WORDS_GENOME&&(c->genome[i] != ~((uintptr_t)0));
					++i) {
						word = c->genome[i];
						for(j=0;j<BITS_IN_WORD/4;++j,word >>= 4) {
					/* We ignore 0xf's here, because otherwise very similar genomes
					* might get quite different hash values in the case when one of
					* the genomes is slightly longer and uses one more maschine
					* word. */
							opcode = word & 0xf;
							if (skipnext)
								skipnext = 0;
							else {
								if (opcode != 0xf)
									sum += opcode;
								if (opcode == 0xc) /* 0xc == XCHG */
									/* Skip "operand" after XCHG */
									skipnext = 1; 
							}
						}
					}
					/* For the hash-value use a wrapped around sum of the sum of all
					* commands and the length of the genome. The + 64 makes the colors a bit brighter. */
					return (uint32_t)((sum % 192) + 64)*256*256*256;
				}
				return 0;
			case LINEAGE:
				/* * Cells with generation > 1 are color-coded by lineage.  */
				return (c->generation > 1) ? (((uint32_t)c->lineage) | (uint32_t)1) : 0;
			case MAX_COLOR_SCHEME:
				/* ... never used... to make compiler shut up. */
				break;
		}
	}
	return 0; /* Cells with no energy are black */
}
#ifdef USE_SDL
static SDL_Event       sdlEvent;               // keyboard, mouse, etc.
static SDL_Window*     sdlWindow;              //  
static SDL_Renderer*   sdlRenderer;
static SDL_Texture*    sdlTexture;
static void*           myPixels;               // Filled in by SDL_LockTexture()
static uint32_t*       dst;
static int             sdlPitch;                  // Filled in by SDL_LockTexture()

static void Initialize_SDL2(){
        if( SDL_Init(0)                 ){  
                fprintf(stderr, "BLR %s::%d Error in SDL_Init(0)\n%s\n", 
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }   

        if( SDL_InitSubSystem( SDL_INIT_TIMER )         ){  
                fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_TIMER)\n%s\n",
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }   
        if( SDL_InitSubSystem( SDL_INIT_VIDEO )         ){  
                fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_VIDEO)\n%s\n",
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }   
        if( SDL_InitSubSystem( SDL_INIT_EVENTS )        ){  
                fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_EVENTS)\n%s\n",
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }   

	atexit(SDL_Quit);

        // SDL Window and Renderer creation

        if( SDL_CreateWindowAndRenderer(
                POND_SIZE_X,                    // width 
                POND_SIZE_Y,                    // height
                SDL_WINDOW_RESIZABLE            // flags 
                | SDL_WINDOW_MAXIMIZED,
                &sdlWindow,                     // window pointer
                &sdlRenderer)                   // render pointer
        ){
                fprintf(stderr, "BLR %s::%d Error in SDL_CreateWindowAndRenderer\n%s\n",
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }

	SDL_SetWindowTitle(sdlWindow, "NanocellArray"); 

        // Create a texture.
        sdlTexture = SDL_CreateTexture(
                sdlRenderer,
                SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,    // access:  STREAMING = Changes frequently, lockable
                POND_SIZE_X, 			// width
                POND_SIZE_Y);			// height
	if(sdlTexture == NULL){
                fprintf(stderr, "BLR %s::%d Error in SDL_CreateTexture()\n%s\n",
                                __FILE__, __LINE__, SDL_GetError());
                exit(-1);
        }   
}

static void RedrawScreen(){
	uintptr_t x,y;
	fprintf(stderr, "Redrawing screen....\n");	
	// Get pixel pointer.
	if( SDL_LockTexture(
		sdlTexture,             // STREAMING texture to access
		NULL,                   // NULL = update entire texture, otherwise SDL_Rect*
		&myPixels,              // raw pixel data
		&sdlPitch)                 // pointer to number of bytes in a row of pixel data.
	){
		fprintf(stderr, "BLR %s::%d Error in SDL_LockTexture\n%s\n",
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}

	for (y=0;y<POND_SIZE_Y;++y) {
		dst = (Uint32*)((Uint8*)myPixels + y * sdlPitch);
		for (x=0;x<POND_SIZE_X;++x)
			*dst++ = getColor(&cellArray[x][y]);
	}

	SDL_UnlockTexture( sdlTexture );
	SDL_RenderClear(sdlRenderer);
	SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
	SDL_RenderPresent(sdlRenderer);
}

#endif

static void timeHandler(struct itimerval tval) {
#ifdef STOP_AT
	printf("\n Time is up. %d seconds have passed.\n", STOP_AT);
#endif
	exit(0);
}

/**
 * Main method
 */
int main(int argc,char **argv)
{
	uintptr_t i,j,x,y;
	int cellPickIndex = POND_SIZE_X * POND_SIZE_Y;	    


#ifdef STOP_AT
	struct itimerval tvalStop;
	tvalStop.it_value.tv_sec = STOP_AT;

	(void) signal(SIGALRM, timeHandler);
	(void) setitimer(ITIMER_REAL, &tvalStop, NULL);
#endif


  
	/* Buffer used for execution output of candidate offspring */
	uintptr_t outputBuf[MAX_WORDS_GENOME];

/**** BEGIN TEST PRINTFS ****/
	
/* Seed and init the original random number generator */
	init_genrand(1234567890);
	for(i=0;i<1024;++i) {// init both methods of RNGs	
	    getRandom();
	}


	register uint64_t c = 0;
	// only works on x86. assembly code to read a random number
	// // into register provided, c
	__asm__ __volatile__ (
	"RDRAND %0;"
	:"=r"(c)
	:
	:
	);

// Measure how long it takes to seed and init the array of RNG arrays
	struct timeval fcnStart, fcnStop;
	gettimeofday(&fcnStart, NULL);

	// Seed and init cell picker RNG
	init_genrandArray(1234567890);
	//init_genrandArray(c);
	for(i=0;i<1024;++i) {// init both methods of RNGs	
		//for (j = 0; j < POND_SIZE_X * POND_SIZE_Y; j++) {
			//printf("random from array index %d: %d\n",j, getRandomFromArray(j));
			//getRandomFromArray(j);
			getRandomFromArray(cellPickIndex);
		//}
	}
	
	gettimeofday(&fcnStop, NULL);

// print out times before and after function, and then the difference
//printf("array rng 1st time: %lf 2nd time: %lf difference: %lf \n", (float) fcnStart.tv_sec, (float) fcnStop.tv_sec, (fcnStop.tv_sec - fcnStart.tv_sec) + (fcnStop.tv_usec - fcnStart.tv_usec)/1000000.0);	

/*  These tests call the random functions, so comment out if all arrays should be on the same index! */
	// compare original and new methods' 32int randoms
	//printf("\noriginal genrand_int32 output: %lu new output: %lu\n", genrand_int32(), genrand_int32Array(4));
	//same comparison print without getRandomFromArray
	//printf("original getRandom output: %lu getRandomFromArray: %lu \n", getRandom(), getRandomFromArray(4));

	// make sure array RNGs are independent
	/*
	//printf("random from array 1: %d\n", getRandomFromArray(1));
	//printf("random from array 2: %d\n", getRandomFromArray(2));
	//printf("random from array 3: %d\n", getRandomFromArray(3));
	*/
	// alternate run to ensure independence
	//printf("random from array 3: %d\n", getRandomFromArray(3));
	//printf("random from array 2: %d\n", getRandomFromArray(2));
	

/**** END TEST PRINTFS ****/
    

	/* Reset per-update stat counters */
	for(x=0;x<sizeof(statCounters);++x)
		((uint8_t *)&statCounters)[x] = (uint8_t)0;
  
 
	/* Clear the cellArray and initialize all genomes
	* to 0xffff... */
	for(x=0;x<POND_SIZE_X;++x) {
		for(y=0;y<POND_SIZE_Y;++y) {
			cellArray[x][y].ID = 0;
			cellArray[x][y].parentID = 0;
			cellArray[x][y].lineage = 0;
			cellArray[x][y].generation = 0;
			cellArray[x][y].energy = 0;
			for(i=0;i<MAX_WORDS_GENOME;++i)
				cellArray[x][y].genome[i] = ~((uintptr_t)0);
		}
	}
  
    uint64_t clock = 0;		/* Clock is incremented on each core loop */
  
	uint64_t cellIdCounter = 0;	/* This is used to generate unique cell IDs */
  
	uintptr_t currentWord,wordPtr,shiftPtr,inst,tmp;/* Miscellaneous variables */
	struct Cell *currCell,*neighborCell;			/* used in the loop */
  
	uintptr_t ptr_wordPtr;	/* Virtual machine memory pointer register (which */
	uintptr_t ptr_shiftPtr; /* exists in two parts... read the code below...) */
	  
	uintptr_t reg;			/* The main "register" */
	uintptr_t facing;		/* Which way is the cell facing? */
  
  
	uintptr_t loopStack_wordPtr[MAX_NUM_INSTR];	/*				  */
	uintptr_t loopStack_shiftPtr[MAX_NUM_INSTR];	/* Virtual machine loop/rep stack */
	uintptr_t loopStackPtr;				/* 				  */
  
	uintptr_t falseLoopDepth; 		/* If this is nonzero, we're skipping to matching REP */
  						/* It is incremented to track the depth of a nested set
   						* of LOOP/REP pairs in false state. */
  
  
  
   
	int stop;			/* If this is nonzero, cell execution stops. This allows us
					* to avoid the ugly use of a goto to exit the loop. :) */
	int currRNG = 0;
	int startRow;	// for testing with parallelEdit

#ifdef USE_SDL
	/* Set up SDL if we're using it */
	Initialize_SDL2();
	RedrawScreen();
#endif /* USE_SDL */

	/* Main loop */
	for(;;) {

 
#ifdef STOP_AT
		//if (clock >= STOP_AT) {		/* Stop at STOP_AT seconds if defined */
		//	fprintf(stderr,"[QUIT] STOP_AT value of %d seconds reached\n", STOP_AT);
		//	break;
		//}
#endif /* STOP_AT */

		/* Increment clock and run updates periodically */
		/* Clock is incremented at the start, so it starts at 1 */
		if (!(++clock % UPDATE_FREQUENCY)) {
			doUpdate(clock);
#ifdef USE_SDL
			/* SDL display is also refreshed every UPDATE_FREQUENCY */
			while (SDL_PollEvent(&sdlEvent)) {
				if (sdlEvent.type == SDL_QUIT) {
					fprintf(stderr,"[QUIT] Quit signal received!\n");
					exit(0);
				} else { 
					if (sdlEvent.type == SDL_MOUSEBUTTONDOWN) {
						switch (sdlEvent.button.button) {
							case SDL_BUTTON_LEFT:
								fprintf(stderr,"[INTERFACE] Genome of cell at (%d, %d):\n",sdlEvent.button.x, sdlEvent.button.y);
								reportCell(stderr, &cellArray[sdlEvent.button.x][sdlEvent.button.y]);
								break;
							case SDL_BUTTON_RIGHT:
								colorScheme = (colorScheme + 1) % MAX_COLOR_SCHEME;
								fprintf(stderr,"[INTERFACE] Switching to color scheme \"%s\".\n",colorSchemeName[colorScheme]);
								RedrawScreen();
								break;
						}
					}
				}
			}
#ifdef USE_SDL
		RedrawScreen();	// Really inefficient, but let's see if it works.
#endif
#endif /* USE_SDL<*/
		}

#ifdef REPORT_FREQUENCY
		/* Periodically report the viable population if defined */
		if (!(clock % REPORT_FREQUENCY))
			doReport(clock);
#endif /* REPORT_FREQUENCY */

#ifdef CYCLE_REPORT_FREQUENCY
		/* Periodically report the viable population if defined */
		//if (!(clock % CYCLE_REPORT_FREQUENCY))
		//	doCycleReport(clock);
#endif /* REPORT_FREQUENCY */


		/* Introduce a random cell somewhere with a given energy level */
		/* This is called seeding, and introduces both energy and
		* entropy into the substrate. This happens every INFLOW_FREQUENCY
		* clock ticks. */
#ifdef SINGLE_RNG
		//startRow = getRandom() % 3;
		if (!(clock % INFLOW_FREQUENCY)) {
			x = getRandom() % POND_SIZE_X;
			y = getRandom() % POND_SIZE_Y;
			
#else
//		startRow = getRandomFromArray(cellPickIndex) % 3;
		if (!(clock % INFLOW_FREQUENCY)) {
			x = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % POND_SIZE_X;
			y = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % POND_SIZE_Y;
#endif
			currCell = &cellArray[x][y];
			currCell->ID = cellIdCounter;
			currCell->parentID = 0;
			currCell->lineage = cellIdCounter;
			currCell->generation = 0;
#ifdef INFLOW_RATE_VARIATION
	#ifdef SINGLE_RNG
			currCell->energy += INFLOW_RATE_BASE + (getRandom() % INFLOW_RATE_VARIATION);
	#else
			currCell->energy += INFLOW_RATE_BASE + (getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % INFLOW_RATE_VARIATION);
	#endif
#else
			currCell->energy += INFLOW_RATE_BASE;
#endif /* INFLOW_RATE_VARIATION */
			for(i=0;i<MAX_WORDS_GENOME;++i) 
#ifdef SINGLE_RNG
				currCell->genome[i] = getRandom();
#else
				currCell->genome[i] = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y);
#endif
			++cellIdCounter;
      
      /* Update the random cell on SDL screen if viz is enabled */
#ifdef USE_SDL_NOTYET
			//FIXME
			if (SDL_MUSTLOCK(screen))
				SDL_LockSurface(screen);
			((uint8_t *)screen->pixels)[x + (y * sdlPitch)] = getColor(currCell);
			if (SDL_MUSTLOCK(screen))
				SDL_UnlockSurface(screen);
#endif /* USE_SDL */
		}
    
		/* Pick a random cell to execute */
#ifdef SINGLE_RNG
		x = getRandom() % POND_SIZE_X;
		y = getRandom() % POND_SIZE_Y;
		//for testing
		//y = startRow + (3 * i);
#else
		x = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % POND_SIZE_X;
		y = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % POND_SIZE_Y;
		//for testing
		//y = startRow + (3 * i);
#endif
		currCell = &cellArray[x][y];
		// sets current RNG to one that correlates with current cell position
		currRNG = x + POND_SIZE_X * y;

		/* Reset the state of the VM prior to execution */
		for(i=0;i<MAX_WORDS_GENOME;++i)
			outputBuf[i] = ~((uintptr_t)0); /* ~0 == 0xfffff... */
		ptr_wordPtr = 0;
		ptr_shiftPtr = 0;
		reg = 0;
		loopStackPtr = 0;
		wordPtr = EXEC_START_WORD;
		shiftPtr = EXEC_START_BIT;
		facing = 0;
		falseLoopDepth = 0;
		stop = 0;

		/* We use a currentWord buffer to hold the word we're
		* currently working on.  This speeds things up a bit
		* since it eliminates a pointer dereference in the
		* inner loop. We have to be careful to refresh this
		* whenever it might have changed... take a look at
		* the code. :) */
		currentWord = currCell->genome[0];
    
		/* Keep track of how many cells have been executed */
		statCounters.cellExecutions += 1.0;

//for RNG testing against parallelEdit
//printf("next random number is: %lu\n", getRandomFromArray(cellPickIndex));
//exit(0);

		/* Core execution loop */
	//	while (currCell->energy&&(!stop)) {
		// for testing
		while (1 > 2) {

			/* Get the next instruction */
			inst = (currentWord >> shiftPtr) & 0xf;
      
			/* Randomly frob either the instruction or the register with a
			* probability defined by MUTATION_RATE. This introduces variation,
			* and since the variation is introduced into the state of the VM
			* it can have all manner of different effects on the end result of
			* replication: insertions, deletions, duplications of entire
			* ranges of the genome, etc. */
#ifdef SINGLE_RNG
			if ((getRandom() & 0xffffffff) < MUTATION_RATE) {
				tmp = getRandom(); /* Call getRandom() only once for speed */
#else
			if ((getRandomFromArray(currRNG) & 0xffffffff) < MUTATION_RATE) {
				tmp = getRandomFromArray(currRNG); /* Call getRandom() only once for speed */
#endif
				//printf("clock cycle: %d currRNG: %d\n", clock, currRNG);
				if (tmp & 0x80) /* Check for the 8th bit to get random boolean */
					inst = tmp & 0xf; /* Only the first four bits are used here */
				else 
					reg = tmp & 0xf;
			}
      
			/* Each instruction processed costs one unit of energy */
			--currCell->energy;
      
			/* Execute the instruction */
			if (falseLoopDepth) {
				/* Skip forward to matching REP if we're in a false loop. */
				if (inst == 0x9) /* Increment false LOOP depth */
					++falseLoopDepth;
				else 
					if (inst == 0xa) /* Decrement on REP */
						--falseLoopDepth;
			} else {
				/* If we're not in a false LOOP/REP, execute normally */
        
				/* Keep track of execution frequencies for each instruction */
				statCounters.instructionExecutions[inst] += 1.0;
        
				switch(inst) {
					case 0x0: /* ZERO: Zero VM state registers */
						reg = 0;
						ptr_wordPtr = 0;
						ptr_shiftPtr = 0;
						facing = 0;
						break;
					case 0x1: /* FWD: Increment the pointer (wrap at end) */
						if ((ptr_shiftPtr += 4) >= BITS_IN_WORD) {
							if (++ptr_wordPtr >= MAX_WORDS_GENOME)
								ptr_wordPtr = 0;
							ptr_shiftPtr = 0;
						}
						break;
					case 0x2: /* BACK: Decrement the pointer (wrap at beginning) */
						if (ptr_shiftPtr)
							ptr_shiftPtr -= 4;
						else {
							if (ptr_wordPtr)
								--ptr_wordPtr;
							else 
								ptr_wordPtr = MAX_WORDS_GENOME - 1;
							ptr_shiftPtr = BITS_IN_WORD - 4;
						}
						break;
					case 0x3: /* INC: Increment the register */
						reg = (reg + 1) & 0xf;
						break;
					case 0x4: /* DEC: Decrement the register */
						reg = (reg - 1) & 0xf;
						break;
					case 0x5: /* READG: Read into the register from genome */
						reg = (currCell->genome[ptr_wordPtr] >> ptr_shiftPtr) & 0xf;
						break;
					case 0x6: /* WRITEG: Write out from the register to genome */
						currCell->genome[ptr_wordPtr] &= ~(((uintptr_t)0xf) << ptr_shiftPtr);
						currCell->genome[ptr_wordPtr] |= reg << ptr_shiftPtr;
						currentWord = currCell->genome[wordPtr]; /* Must refresh in case this changed! */
						break;
					case 0x7: /* READB: Read into the register from buffer */
						reg = (outputBuf[ptr_wordPtr] >> ptr_shiftPtr) & 0xf;
						break;
					case 0x8: /* WRITEB: Write out from the register to buffer */
						outputBuf[ptr_wordPtr] &= ~(((uintptr_t)0xf) << ptr_shiftPtr);
						outputBuf[ptr_wordPtr] |= reg << ptr_shiftPtr;
						break;
					case 0x9: /* LOOP: Jump forward to matching REP if register is zero */
						if (reg) {
							if (loopStackPtr >= MAX_NUM_INSTR)
								stop = 1; /* Stack overflow ends execution */
							else {
								loopStack_wordPtr[loopStackPtr] = wordPtr;
								loopStack_shiftPtr[loopStackPtr] = shiftPtr;
								++loopStackPtr;
							}
						} else falseLoopDepth = 1;
						break;
					case 0xa: /* REP: Jump back to matching LOOP if register is nonzero */
						if (loopStackPtr) {
							--loopStackPtr;
							if (reg) {
								wordPtr = loopStack_wordPtr[loopStackPtr];
								shiftPtr = loopStack_shiftPtr[loopStackPtr];
								currentWord = currCell->genome[wordPtr];
								/* This ensures that the LOOP is rerun */
								continue;
							}
						}
						break;
					case 0xb: /* TURN: Turn in the direction specified by register */
						facing = reg & 3;
						break;
					case 0xc: /* XCHG: Skip next instruction and exchange value of register with it */
						if ((shiftPtr += 4) >= BITS_IN_WORD) {
							if (++wordPtr >= MAX_WORDS_GENOME) {
								wordPtr = EXEC_START_WORD;
								shiftPtr = EXEC_START_BIT;
							} else shiftPtr = 0;
						}
						tmp = reg;
						reg = (currCell->genome[wordPtr] >> shiftPtr) & 0xf;
						currCell->genome[wordPtr] &= ~(((uintptr_t)0xf) << shiftPtr);
						currCell->genome[wordPtr] |= tmp << shiftPtr;
						currentWord = currCell->genome[wordPtr];
						break;
					case 0xd: /* KILL: Blow away neighboring cell if allowed with penalty on failure */
						neighborCell = getNeighbor(x,y,facing);
						if (accessAllowed(neighborCell,reg,0,currRNG)) {
							if (neighborCell->generation > 2)
								++statCounters.viableCellsKilled;

							/* Filling first two words with 0xfffff... is enough */
							neighborCell->genome[0] = ~((uintptr_t)0);
							neighborCell->genome[1] = ~((uintptr_t)0);
							neighborCell->ID = cellIdCounter;
							neighborCell->parentID = 0;
							neighborCell->lineage = cellIdCounter;
							neighborCell->generation = 0;
							++cellIdCounter;
						} else {
							if (neighborCell->generation > 2) {
								tmp = currCell->energy / FAILED_KILL_PENALTY;
								if (currCell->energy > tmp)
									currCell->energy -= tmp;
								else 
									currCell->energy = 0;
							}
						}
						break;
					case 0xe: /* SHARE: Equalize energy between self and neighbor if allowed */
						neighborCell = getNeighbor(x,y,facing);
						if (accessAllowed(neighborCell,reg,1,currRNG)) {
							if (neighborCell->generation > 2)
								++statCounters.viableCellShares;

							tmp = currCell->energy + neighborCell->energy;
							neighborCell->energy = tmp / 2;
							currCell->energy = tmp - neighborCell->energy;
						}
						break;
					case 0xf: /* STOP: End execution */
						stop = 1;
						break;
				} // end switch
			} // end else 

			/* Advance the shift and word pointers, and loop around
			* to the beginning at the end of the genome. */
			if ((shiftPtr += 4) >= BITS_IN_WORD) {
				if (++wordPtr >= MAX_WORDS_GENOME) {
					wordPtr = EXEC_START_WORD;
					shiftPtr = EXEC_START_BIT;
				} else {
					shiftPtr = 0;
				}
				currentWord = currCell->genome[wordPtr];
			}
		} //end core execution loop
    
		/* Copy outputBuf into neighbor if access is permitted and there
		* is energy. There is no need to copy to a cell with no energy, since anything copied there
		* would never be executed and then would be replaced with random
		* junk eventually. See the seeding code in the main loop above. */
		if ((outputBuf[0] & 0xff) != 0xff) {
            neighborCell = getNeighbor(x,y,facing);
			if ((neighborCell->energy)&&accessAllowed(neighborCell,reg,0,currRNG)) {
				/* If replacing a viable cell, update relevant stats for update */
				if (neighborCell->generation > 2)
					++statCounters.viableCellsReplaced;
                
                // Generate new cell ID and set parent ID, lineage, generation accordingly
				neighborCell->ID = ++cellIdCounter;
				neighborCell->parentID = currCell->ID;
				neighborCell->lineage = currCell->lineage; /* Lineage is copied in offspring */
				neighborCell->generation = currCell->generation + 1;
                // outputBuf becomes the new cell's genome
				for(i=0;i<MAX_WORDS_GENOME;++i)
					neighborCell->genome[i] = outputBuf[i];
			}
		}

		/* Update the cellArray on SDL screen to show any changes since last instruction. */
#ifdef USE_SDL_NOTYET
		//FIXME
		if (SDL_MUSTLOCK(screen))
			SDL_LockSurface(screen);
		((uint8_t *)screen->pixels)[x + (y * sdlPitch)] = getColor(currCell);
		if (x) {
			((uint8_t *)screen->pixels)[(x-1) + (y * sdlPitch)] = getColor(&cellArray[x-1][y]);
			if (x < (POND_SIZE_X-1))
				((uint8_t *)screen->pixels)[(x+1) + (y * sdlPitch)] = getColor(&cellArray[x+1][y]);
			else 
				((uint8_t *)screen->pixels)[y * sdlPitch] = getColor(&cellArray[0][y]);
		} else {
			((uint8_t *)screen->pixels)[(POND_SIZE_X-1) + (y * sdlPitch)] = getColor(&cellArray[POND_SIZE_X-1][y]);
			((uint8_t *)screen->pixels)[1 + (y * sdlPitch)] = getColor(&cellArray[1][y]);
		}
		if (y) {
			((uint8_t *)screen->pixels)[x + ((y-1) * sdlPitch)] = getColor(&cellArray[x][y-1]);
			if (y < (POND_SIZE_Y-1))
				((uint8_t *)screen->pixels)[x + ((y+1) * sdlPitch)] = getColor(&cellArray[x][y+1]);
			else 
				((uint8_t *)screen->pixels)[x] = getColor(&cellArray[x][0]);
		} else {
			((uint8_t *)screen->pixels)[x + ((POND_SIZE_Y-1) * sdlPitch)] = getColor(&cellArray[x][POND_SIZE_Y-1]);
			((uint8_t *)screen->pixels)[x + sdlPitch] = getColor(&cellArray[x][1]);
		}
		if (SDL_MUSTLOCK(screen))
			SDL_UnlockSurface(screen);
#endif /* USE_SDL */

	/*
	printf("clock is %lu", clock);
	//for testing
	if (clock >= 319) {
		
		printf("next random number is: %lu\n", getRandomFromArray(cellPickIndex));
		exit(0);
	}
*/
	} // end infinite for loop (core execution loop carrying out instructions)
  

	
	return 0; /* Silences compiler warning */
    
}
