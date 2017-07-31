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
#define STOP_AT 1000000
#define UPDATE_FREQUENCY 1000000
#define REPORT_FREQUENCY 100000000
#define CLOCKUPDATE_FREQUENCY 10000
#define CLOCKREPORT_FREQUENCY 10000000
#define MUTATION_RATE 21475
#define INFLOW_FREQUENCY 100
#define INFLOW_RATE_BASE 4000
#define INFLOW_RATE_VARIATION 8000
#define POND_SIZE_X 640
#define POND_SIZE_Y 480
#define MAX_NUM_INSTR 512
#define FAILED_KILL_PENALTY 2

#define BATCH_SIZE 100

#define MAX_WORDS_GENOME (MAX_NUM_INSTR / (sizeof(uintptr_t) * 2))
#define BITS_IN_WORD (sizeof(uintptr_t) * 8)
#define N_LEFT 0
#define N_RIGHT 1
#define N_UP 2
#define N_DOWN 3
#define EXEC_START_WORD 0
#define EXEC_START_BIT 4

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
            //rngArray[i][0] = (s + i) & 0xffffffffUL;
            rngArray[i][0] = (s) & 0xffffffffUL;
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
        //return (uintptr_t)((((uint64_t)genrand_int32Array(whichRNG)) << 32) ^ ((uint64_t)genrand_int32Array(whichRNG)));
        result = (uintptr_t)((((uint64_t)genrand_int32Array(whichRNG)) << 32) ^ ((uint64_t)genrand_int32Array(whichRNG)));
    } else {
        //return (uintptr_t)genrand_int32Array(whichRNG);
        result = (uintptr_t)genrand_int32Array(whichRNG);
    }
    //printf("random %lu drawn\n", result);
    return result;
}

//central structures
static const uintptr_t BITS_IN_FOURBIT_WORD[16] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

struct Cell {
	uint64_t ID;            /* Globally unique cell ID */
	uint64_t parentID;      /* ID of the cell's parent */
	uint64_t lineage;       /* Equal to the cell ID of the first cell in the line. */
	uintptr_t generation;       /* Generations start at 0 and are incremented from there. */
	uintptr_t energy;       /* Energy level of this cell */
	uintptr_t genome[MAX_WORDS_GENOME];/* four-bit instructions packed into machine-size words */
};

struct Cell cellArray[POND_SIZE_X][POND_SIZE_Y];

struct PerUpdateStatCounters
{
	double instructionExecutions[16];/* Per-instruction-type execution count since last update. */
        double cellExecutions;      /* Number of cells executed since last update */
	uintptr_t viableCellsReplaced;  /* Number of viable cells replaced by other cells' offspring */
	uintptr_t viableCellsKilled;    /* Number of viable cells KILLed */
	uintptr_t viableCellShares; /* Number of successful SHARE operations */
};

struct PerUpdateStatCounters statCounters; 


static void doClockUpdate(const uint64_t clock)
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

static void doClockReport(const uint64_t clock)
{
	char buf[MAX_NUM_INSTR*2];
	FILE *d;
	uintptr_t x,y,wordPtr,shiftPtr,inst,stopCount,i;
	struct Cell *currCell;
  
	sprintf(buf,"c%lu.report.csv",clock);
	d = fopen(buf,"w");
	if (!d) {
		fprintf(stderr,"[WARNING] Could not open %s for writing.\n",buf);
		return;
	}
  
	fprintf(stderr,"[INFO] Reporting viable cells to %s\n",buf);
  
	for(x=0;x<POND_SIZE_X;++x) {
		for(y=0;y<POND_SIZE_Y;++y) {
			currCell = &cellArray[x][y];
			//if (currCell->energy&&(currCell->generation > 2)) {
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
			//}
		}
	}
}
static void doReport(const uint64_t clock)
{
	char buf[MAX_NUM_INSTR*2];
	FILE *d;
	uintptr_t x,y,wordPtr,shiftPtr,inst,stopCount,i;
	struct Cell *currCell;
  
	sprintf(buf,"p%lu.report.csv",clock);
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
			}
		}
	}
}

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
		? (((getRandomFromArray(currRNG) & 0xf) >= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID)) 
		: (((getRandomFromArray(currRNG) & 0xf) <= 
			BITS_IN_FOURBIT_WORD[(c2->genome[0] & 0xf) ^ (c1guess & 0xf)])||(!c2->parentID));
}



//array of locations where the threads can go to get the location of a random cell
unsigned long randomLocationX[BATCH_SIZE];
unsigned long randomLocationY[BATCH_SIZE];

int cellConflicts[POND_SIZE_X][POND_SIZE_Y];
int cellPickIndex = POND_SIZE_X * POND_SIZE_Y;

void pickBatch() {
        int i;
for (i = 0; i < BATCH_SIZE; i++) {     
        int x = getRandomFromArray(cellPickIndex) % POND_SIZE_X;
        int y = getRandomFromArray(cellPickIndex) % POND_SIZE_Y;
    /* commented out to choose locations randomly 
   memset(cellConflicts, 0, sizeof(cellConflicts));

        while (cellConflicts[x][y] > 0) { 
                x = getRandomFromArray(cellPickIndex) % POND_SIZE_X;
                y = getRandomFromArray(cellPickIndex) % POND_SIZE_Y;
        }    

        //marks adjacent cells as taken for that batch
        cellConflicts[x][y] = 1; 
        cellConflicts[x + 1][y] = 1; 
        cellConflicts[x - 1][y] = 1; 
        cellConflicts[x][y + 1] = 1; 
        cellConflicts[x][y - 1] = 1; 
     */
        randomLocationX[i] = x; 
        randomLocationY[i] = y; 

        //printf("random location %d is x: %lu y: %lu\n", i, randomLocationX[i], randomLocationY[i]);
        }    
}

void executeCell(int x, int y) {
	uintptr_t ptr_wordPtr = 0; 
    	uintptr_t ptr_shiftPtr = 0; 
    	uintptr_t reg = 0; 
    	uintptr_t loopStack_wordPtr[MAX_NUM_INSTR];
  	uintptr_t loopStack_shiftPtr[MAX_NUM_INSTR];
	uintptr_t loopStackPtr = 0; 
    	uintptr_t wordPtr = EXEC_START_WORD;
    	uintptr_t shiftPtr = EXEC_START_BIT;
    	uintptr_t facing = 0; 
    	uintptr_t falseLoopDepth = 0; 
    	int stop = 0; 		
	uintptr_t currentWord; 
	uintptr_t inst, tmp;
	struct Cell *currCell = &cellArray[x][y];
	struct Cell *neighborCell = getNeighbor(x, y, facing); 
		
	int currRNG = x + POND_SIZE_X * y;
	currentWord = currCell->genome[0];
		
	int i;
	uintptr_t outputBuf[MAX_WORDS_GENOME];
	for(i=0;i<MAX_WORDS_GENOME;++i)
      		outputBuf[i] = ~((uintptr_t)0);
	uint64_t instrExecs[16] = {0}; 
        uint64_t cellsReplaced = 0; 
        uint64_t cellsKilled = 0; 
        uint64_t cellsShared = 0; 
		
    while (currCell->energy&&(!stop)) {
      inst = (currentWord >> shiftPtr) & 0xf;
      instrExecs[inst] += 1.0;

      if ((getRandomFromArray(currRNG) & 0xffffffff) < MUTATION_RATE) {
        tmp = getRandomFromArray(currRNG); 
        if (tmp & 0x80) // Check for the 8th bit to get random boolean //
          inst = tmp & 0xf; // Only the first four bits are used here //
        else reg = tmp & 0xf;
      }
      
      --currCell->energy;
      
      if (falseLoopDepth) {
        if (inst == 0x9)
          ++falseLoopDepth;
        else if (inst == 0xa) 
          --falseLoopDepth;
      } else {
        switch(inst) { 
          case 0x0: // ZERO: Zero VM state registers //
            reg = 0;
            ptr_wordPtr = 0;
            ptr_shiftPtr = 0;
            facing = 0;
            break;
          case 0x1: // FWD: Increment the pointer (wrap at end) //
            if ((ptr_shiftPtr += 4) >= BITS_IN_WORD) {
              if (++ptr_wordPtr >= MAX_WORDS_GENOME)
                ptr_wordPtr = 0;
              ptr_shiftPtr = 0;
            }
            break;
          case 0x2: // BACK: Decrement the pointer (wrap at beginning) //
            if (ptr_shiftPtr)
              ptr_shiftPtr -= 4;
            else {
              if (ptr_wordPtr)
                --ptr_wordPtr;
              else ptr_wordPtr = MAX_WORDS_GENOME - 1;
              ptr_shiftPtr = BITS_IN_WORD - 4;
            }
            break;
          case 0x3: // INC: Increment the register //
            reg = (reg + 1) & 0xf;
            break;
          case 0x4: // DEC: Decrement the register //
            reg = (reg - 1) & 0xf;
            break;
          case 0x5: // READG: Read into the register from genome //
            reg = (currCell->genome[ptr_wordPtr] >> ptr_shiftPtr) & 0xf;
            break;
          case 0x6: // WRITEG: Write out from the register to genome //
            currCell->genome[ptr_wordPtr] &= ~(((uintptr_t)0xf) << ptr_shiftPtr);
            currCell->genome[ptr_wordPtr] |= reg << ptr_shiftPtr;
            currentWord = currCell->genome[wordPtr]; // Must refresh in case this changed! //
            break;
          case 0x7: // READB: Read into the register from buffer //
            reg = (outputBuf[ptr_wordPtr] >> ptr_shiftPtr) & 0xf;
            break;
          case 0x8: // WRITEB: Write out from the register to buffer //
            outputBuf[ptr_wordPtr] &= ~(((uintptr_t)0xf) << ptr_shiftPtr);
            outputBuf[ptr_wordPtr] |= reg << ptr_shiftPtr;
            break;
          case 0x9: // LOOP: Jump forward to matching REP if register is zero //
            if (reg) {
              if (loopStackPtr >= MAX_NUM_INSTR)
                stop = 1; // Stack overflow ends execution //
              else {
                loopStack_wordPtr[loopStackPtr] = wordPtr;
                loopStack_shiftPtr[loopStackPtr] = shiftPtr;
                ++loopStackPtr;
              }
            } else falseLoopDepth = 1;
            break;
          case 0xa: // REP: Jump back to matching LOOP if register is nonzero //
            if (loopStackPtr) {
              --loopStackPtr;
              if (reg) {
                wordPtr = loopStack_wordPtr[loopStackPtr];
                shiftPtr = loopStack_shiftPtr[loopStackPtr];
                currentWord = currCell->genome[wordPtr];
                // This ensures that the LOOP is rerun //
                continue;
              }
            }
            break;
          case 0xb: // TURN: Turn in the direction specified by register //
            facing = reg & 3;
            break;
          case 0xc: // XCHG: Skip next instruction and exchange value of register with it //
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
          case 0xd: // KILL: Blow away neighboring cell if allowed with penalty on failure //
            neighborCell = getNeighbor(x,y,facing);
            if (accessAllowed(neighborCell,reg,0,currRNG)) {
              if (neighborCell->generation > 2)
                ++cellsKilled;

              // Filling first two words with 0xfffff... is enough //
              neighborCell->genome[0] = ~((uintptr_t)0);
              neighborCell->genome[1] = ~((uintptr_t)0);
              //neighborCell->ID = cellIDCounter;
              neighborCell->parentID = 0;
              //neighborCell->lineage = cellIDCounter;
              neighborCell->generation = 0;
              //++cellIDCounter;
            } else if (neighborCell->generation > 2) {
              tmp = currCell->energy / FAILED_KILL_PENALTY;
              if (currCell->energy > tmp)
                currCell->energy -= tmp;
              else currCell->energy = 0;
            }
            break;
          case 0xe: // SHARE: Equalize energy between self and neighbor if allowed //
            neighborCell = getNeighbor(x,y,facing);
            if (accessAllowed(neighborCell,reg,1,currRNG)) {
              if (neighborCell->generation > 2)
                ++cellsShared;

              tmp = currCell->energy + neighborCell->energy;
              neighborCell->energy = tmp / 2;
              currCell->energy = tmp - neighborCell->energy;
            }
            break;
          case 0xf: // STOP: End execution //
            stop = 1;
            break;
        } // end switch
      } // end else for falseLoopDepth
      
      if ((shiftPtr += 4) >= BITS_IN_WORD) {
        if (++wordPtr >= MAX_WORDS_GENOME) {
          wordPtr = EXEC_START_WORD;
          shiftPtr = EXEC_START_BIT;
        } else shiftPtr = 0;
        currentWord = currCell->genome[wordPtr];
      } // end if
    } // end while

   if ((outputBuf[0] & 0xff) != 0xff) {
        if ((neighborCell->energy)&&accessAllowed(neighborCell,reg,0,currRNG)) {
        	if (neighborCell->generation > 2)
          		++cellsReplaced;

        	//neighborCell->ID = ++cellIDCounter;
        	neighborCell->parentID = currCell->ID;
        	neighborCell->lineage = currCell->lineage; 
        	neighborCell->generation = currCell->generation + 1;
        	for(i=0;i<MAX_WORDS_GENOME;++i)
          		neighborCell->genome[i] = outputBuf[i];
      	}
   }

}



static void timeHandler(struct itimerval tval) {
#ifdef STOP_AT
	printf("\n Time is up. %d seconds have passed.\n", STOP_AT);
#endif
	exit(0);
}

//main
int main()  {
	struct timeval runStart, runStop;
	gettimeofday(&runStart, NULL);

#ifdef STOP_AT
	struct itimerval tvalStop;
	tvalStop.it_value.tv_sec = STOP_AT;

	(void) signal(SIGALRM, timeHandler);
	(void) setitimer(ITIMER_REAL, &tvalStop, NULL);
#endif

	uintptr_t i,j,x,y;
	int cellPickIndex = POND_SIZE_X * POND_SIZE_Y;
	struct Cell *currCell;
	uint64_t clock = 0;
	// TODO: allow all cells access to id counter somehow
	uint64_t cellIDCounter = 0;

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

    //Seeding RNG with assembly instruction
	register uint64_t c = 0;
	// only works on x86. assembly code to read a random number
	// // into register provided, c
	__asm__ __volatile__ (
	"RDRAND %0;"
	:"=r"(c)
	:
	:
	);
    // Seeding and initializing cell picker RNG
    //init_genrandArray(c);
    init_genrandArray(1234567890);
    for(i=0;i<1024;++i)
	getRandomFromArray(cellPickIndex);

    // Batch execution loop
    for (;;){

	
	// Set timer to start measuring how long batch picking and parallel loop takes
	struct timeval fcnStart, fcnStop;
	gettimeofday(&fcnStart, NULL);

	// picking next BATCH_SIZE random locations to execute
	pickBatch();

// Parallel for loop to execute each cell
#pragma omp parallel private(i) 
{
        #pragma omp for  
        for (i = 0; i < BATCH_SIZE; i++) {
		executeCell(randomLocationX[i], randomLocationY[i]);

            //printf("current thread %d\n", omp_get_thread_num());
            //int cellIndex;
            //cellIndex = randomLocationX[i] + POND_SIZE_X * randomLocationY[i];
            //printf("cellIndex %lu is %lu\n", i, cellIndex);
            //printf("rng %lu printing rn %lu\n", cellIndex, getRandomFromArray(cellIndex));
        
    	}
}

	// Finish timing parallel loop and print out time taken
	gettimeofday(&fcnStop, NULL);
//	printf("array rng 1st time: %lf 2nd time: %lf difference: %lf \n", (float) fcnStart.tv_sec, (float) fcnStop.tv_sec, (fcnStop.tv_sec - fcnStart.tv_sec) + (fcnStop.tv_usec - fcnStart.tv_usec)/1000000.0); 

	// Increment clock by batch size
	clock += BATCH_SIZE;
	
	// Introduce random cell with energy. Do this as many times as needed relative to batch size.	
	for (i = 0; i < BATCH_SIZE / INFLOW_FREQUENCY; i++) {
	x = getRandomFromArray(cellPickIndex) % POND_SIZE_X;
	y = getRandomFromArray(cellPickIndex) % POND_SIZE_Y;
	currCell = &cellArray[x][y];
	currCell->ID = cellIDCounter;
	currCell->parentID = 0;
	currCell->lineage = cellIDCounter;
	currCell->generation = 0;
#ifdef INFLOW_RATE_VARIATION
	currCell->energy += INFLOW_RATE_BASE + (getRandomFromArray(POND_SIZE_X * POND_SIZE_Y) % INFLOW_RATE_VARIATION);
#else
	currCell->energy += INFLOW_RATE_BASE;
#endif
	for(i=0;i<MAX_WORDS_GENOME;++i) 
		currCell->genome[i] = getRandomFromArray(POND_SIZE_X * POND_SIZE_Y);
	++cellIDCounter;
	}

 #ifdef STOP_AT
        if ((clock >= STOP_AT)) {
                
		gettimeofday(&runStop, NULL);
		printf("run start: %lf run stop: %lf difference: %lf \n", (float) runStart.tv_sec, (float) runStop.tv_sec, (runStop.tv_sec - runStart.tv_sec) + (runStop.tv_usec - runStart.tv_usec)/1000000.0); 
		exit(0);
	}
#endif 

//testing for consistent RNGs after 1 complete batch cycle
//printf("clock is %lu; next random number is: %lu\n", clock, getRandomFromArray(cellPickIndex));

	//exit(0);    //ends forever loop after first batch
	//printf("batch completed\n");
	// Do updates and reports at defined intervals
        if (!(clock % CLOCKUPDATE_FREQUENCY)) 
                doClockUpdate(clock);
        if (!(clock % CLOCKREPORT_FREQUENCY)) 
                doClockReport(clock);
        if (!(clock % UPDATE_FREQUENCY))
                doUpdate(clock);
        if (!(clock % REPORT_FREQUENCY))
                doReport(clock);
    } // end batch execution loop
	exit(0);
}
