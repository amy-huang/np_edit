// So back in 1989 I'm teaching myself C to draw fractals on my screen
// and it's too slow, so I started teaching myself enough assembly and
// BIOS to speed it up.  It worked.
//
// Almost 30 years later I'm trying to port nanopond to SDL2 and once 
// again trying to puzzle out how to get a blue dot to appear on the
// screen where I intended.  The SDL wiki documentation, while a good
// start, suffers from far too many undefined terms.  The code below is
// cobbled together from that documentation and (far more useful) the
// teststreaming.c code in the SDL2 repo.
//
// https://wiki.libsdl.org/Introduction

#include <SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

int main(){
	SDL_Event 	sdlEvent;		// keyboard, mouse, etc.
	SDL_Window*	sdlWindow;		// 
	SDL_Renderer*	sdlRenderer;
	SDL_Texture*	sdlTexture;
	uint32_t	x,y,z=0;
	void*		myPixels;		// Filled in by SDL_LockTexture()
	uint32_t*	dst;
	int 		pitch;			// Filled in by SDL_LockTexture()

	


	// SDL Initialization 
	if( SDL_Init(0)			){
		fprintf(stderr, "BLR %s::%d Error in SDL_Init(0)\n%s\n", 
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}

	if( SDL_InitSubSystem( SDL_INIT_TIMER ) 	){
		fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_TIMER)\n%s\n",
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}
	if( SDL_InitSubSystem( SDL_INIT_VIDEO ) 	){
		fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_VIDEO)\n%s\n",
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}
	if( SDL_InitSubSystem( SDL_INIT_EVENTS )	){
		fprintf(stderr, "BLR %s::%d Error in SDL_Init(SDL_INIT_EVENTS)\n%s\n",
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}

	// SDL Window and Renderer creation
	
	if( SDL_CreateWindowAndRenderer(
		640,				// width 
		480, 				// height
		SDL_WINDOW_RESIZABLE		// flags 
		| SDL_WINDOW_MAXIMIZED,
		&sdlWindow, 			// window pointer
		&sdlRenderer) 			// render pointer
	){
		fprintf(stderr, "BLR %s::%d Error in SDL_CreateWindowAndRenderer\n%s\n",
				__FILE__, __LINE__, SDL_GetError());
		exit(-1);
	}
	     

	
	// Create a texture.
	sdlTexture = SDL_CreateTexture(
		sdlRenderer, 
		SDL_PIXELFORMAT_ARGB8888, 
		SDL_TEXTUREACCESS_STREAMING, 	// access:  STREAMING = Changes frequently, lockable
		640, 				// width
		480);				// height


	while(1){
		while (SDL_PollEvent(&sdlEvent)) {
			if (sdlEvent.type == SDL_QUIT) {
				fprintf(stderr,"[QUIT] Quit signal received!\n");
				exit(0);
			}
		}

		// Lock-write-unlock cycle.
		if( SDL_LockTexture(
			sdlTexture,		// STREAMING texture to access
			NULL,			// NULL = update entire texture, otherwise SDL_Rect*
			&myPixels,		// raw pixel data
			&pitch)			// pointer to number of bytes in a row of pixel data.
		){
			fprintf(stderr, "BLR %s::%d Error in SDL_LockTexture\n%s\n",
					__FILE__, __LINE__, SDL_GetError());
			exit(-1);
		}

		for( y=0; y<480; y++ ){
			dst = (Uint32*)((Uint8*)myPixels + y * pitch); // dst is the start of the current row.
			for( x=0; x<640; x++ ){
				//*dst++ = 0xFF00FF00;
				*dst++ = (x*x*y*y)/(z?z:1);
			}
		}
		z++;

		SDL_UnlockTexture( sdlTexture );
		      

		// Wipe out existing video framebuffer
		SDL_RenderClear(sdlRenderer);

		// Move texture's contents to the video framebuffer, scaling as needed.
		SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);

		// Display framebuffer on the screen.
		SDL_RenderPresent(sdlRenderer);
	}
	return 0;
}
