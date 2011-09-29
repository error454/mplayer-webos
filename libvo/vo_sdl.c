/*
 *  vo_sdl.c
 *
 *  (was video_out_sdl.c from OMS project/mpeg2dec -> http://linuxvideo.org)
 *
 *  Copyright (C) Ryan C. Gordon <icculus@lokigames.com> - April 22, 2000
 *
 *  Copyright (C) Felix Buenemann <atmosfear@users.sourceforge.net> - 2001
 *
 *  (for extensive code enhancements)
 *
 *  Current maintainer for MPlayer project (report bugs to that address):
 *    Felix Buenemann <atmosfear@users.sourceforge.net>
 *
 *  This file is a video out driver using the SDL library (http://libsdl.org/),
 *  to be used with MPlayer, further info from http://www.mplayerhq.hu
 *
 *  -- old disclaimer --
 *
 *  A mpeg2dec display driver that does output through the
 *  Simple DirectMedia Layer (SDL) library. This effectively gives us all
 *  sorts of output options: X11, SVGAlib, fbcon, AAlib, GGI. Win32, MacOS
 *  and BeOS support, too. Yay. SDL info, source, and binaries can be found
 *  at http://slouken.devolution.com/SDL/
 *
 *  -- end old disclaimer --
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* define to force software-surface (video surface stored in system memory)*/
/* define to enable surface locks, this might be needed on SMP machines */
#undef SDL_ENABLE_LOCKS

#define MONITOR_ASPECT 4.0/3.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"
#include "mp_msg.h"
#include "mp_msg.h"
#include "help_mp.h"
#include "video_out.h"
#include "video_out_internal.h"

#include "fastmemcpy.h"
#include "sub/sub.h"
#include "aspect.h"
#include "libmpcodecs/vfcap.h"

#include "input/input.h"
#include "input/mouse.h"
#include "subopt-helper.h"
#include "mp_fifo.h"
#include <SDL/SDL.h>
#include <SDL/SDL_opengles.h>
#include <SDL/SDL_opengles_ext.h>


int scale;
GLuint texture = 0;
GLushort indices[] = { 0, 1, 2, 1, 2, 3 };

GLint loc_tex_y;
GLint loc_tex_u;
GLint loc_tex_v;

GLuint program;

// Attribute locations
GLint  position;
GLint  texCoord;

// Sampler location
GLint samplerLoc;
float vertexCoords[8];
float scale_n[8] =
{
	0.8, 1,
	-0.8, 1,
	0.8, -1,
	-0.8, -1
};
float scale_1[8] =
{
	1, 1,
	-1, 1,
	1, -1,
	-1, -1
};

float scale_2[8] =
{
	1, 0.9,
	-1, 0.9,
	1, -0.9,
	-1, -0.9
};

float texCoords[] =
{
	1.0, 0.0,
	0.0, 0.0,
	1.0, 1.0,
	0.0, 1.0
};

static const vo_info_t info =
{
	"SDL YUV/RGB/BGR renderer (SDL v1.1.7+ only!)",
	"sdl",
	"Ryan C. Gordon <icculus@lokigames.com>, Felix Buenemann <atmosfear@users.sourceforge.net>",
	""
};

const LIBVO_EXTERN(sdl)

GLuint LoadShaderProgram(const char *p1, const char *p2)
{
	GLuint vshader;
	GLuint fshader;
	GLuint program;

	vshader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vshader,1,&p1,NULL);
	glCompileShader(vshader);

	fshader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fshader,1,&p2,NULL);
	glCompileShader(fshader);

	program = glCreateProgram();
	glAttachShader(program,fshader);
	glAttachShader(program,vshader);
	glLinkProgram(program);

	glDeleteShader ( vshader );
	glDeleteShader ( fshader );

	return program;

}

void GL_Init()
{
	// setup 2D gl environment
	
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );//black background
	

	glDisable(GL_DEPTH_TEST);
	glDepthFunc( GL_ALWAYS );
	
	glDisable(GL_CULL_FACE);
	

	GLbyte vShaderStr[] =  
	    "attribute vec4 a_position;   \n"
	    "attribute vec2 a_texCoord;   \n"
	    "varying vec2 v_texCoord;     \n"
	    "void main()                  \n"
	    "{                            \n"
	    "   gl_Position = a_position; \n"
	    "   v_texCoord = a_texCoord;  \n"
	    "}                            \n";

	GLbyte fShaderStr[] =  
	    "precision mediump float;                            \n"
	    "varying vec2 v_texCoord;                            \n"
	    "uniform sampler2D s_texture;                        \n"
	    "void main()                                         \n"
	    "{                                                   \n"
	    "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
	    "}                                                   \n";

	//http://slouken.blogspot.com/2011/02/mpeg-acceleration-with-glsl.html  	
	GLbyte YUVShader[] =
	    "precision mediump float;                		 \n"
	    "varying vec2 tcoord;                		 \n"
	    "const vec3 v_offset = vec3(-0.0625, -0.5, -0.5);	 \n"
	    "uniform sampler2D Ytex, Utex, Vtex;     	 	 \n"
	    "const vec3 Rcoeff = vec3(1.164,  0.000,  1.596);    \n"
	    "const vec3 Gcoeff = vec3(1.164, -0.391, -0.813);	 \n"
	    "const vec3 Bcoeff = vec3(1.164,  2.018,  0.000);	 \n"
	    "void main(void)                     	 	 \n"
	    "{                            			 \n"
	    "vec3 yuv, rgb;					 \n"
	    "yuv.x = texture2D(tex0, tcoord).r;			 \n"
    	    "tcoord *= 0.5;					 \n"
    	    "yuv.y = texture2D(tex1, tcoord).r;			 \n"
    	    "yuv.z = texture2D(tex2, tcoord).r;			 \n"
	    "yuv += v_offset;					 \n"
    	    "rgb.r = dot(yuv, Rcoeff);				 \n"
    	    "rgb.g = dot(yuv, Gcoeff);				 \n"
    	    "rgb.b = dot(yuv, Bcoeff);				 \n"
	    "gl_FragColor = vec4(rgb, 1.0);			 \n"
	    "}                            			 \n"  ;

	program = LoadShaderProgram(( char *)vShaderStr, (char *)fShaderStr);
	

	position = glGetAttribLocation ( program, "a_position" );
	
	texCoord = glGetAttribLocation ( program, "a_texCoord" );

	loc_tex_y = glGetUniformLocation (program, "Ytex");
	loc_tex_u = glGetUniformLocation (program, "Utex");
	loc_tex_v = glGetUniformLocation (program, "Vtex");	

	samplerLoc = glGetUniformLocation ( program, "s_texture" );
	
}
void GL_InitTexture(int width, int height)
{
	glGenTextures(1, &texture);
	
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	
	//TODO for YUV
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
}


/** Private SDL Data structure **/

static struct sdl_priv_s {

	/* output driver used by sdl */
	char driver[8];

	/* SDL display surface */
	SDL_Surface *surface;

	/* SDL RGB surface */
	SDL_Surface *rgbsurface;

	/* SDL YUV overlay */
	SDL_Overlay *overlay;

	/* available fullscreen modes */
	SDL_Rect **fullmodes;

	/* surface attributes for fullscreen and windowed mode */
	Uint32 sdlflags, sdlfullflags;

	/* save the windowed output extents */
	SDL_Rect windowsize;

	/* Bits per Pixel */
	Uint8 bpp;

	/* RGB or YUV? */
	Uint8 mode;
	#define YUV 0
	#define RGB 1
	#define BGR 2

	/* use direct blitting to surface */
	int dblit;

	/* current fullscreen mode, 0 = highest available fullscreen mode */
	int fullmode;

	/* YUV ints */
	int framePlaneY, framePlaneUV, framePlaneYUY;
	int stridePlaneY, stridePlaneUV, stridePlaneYUY;

	/* RGB ints */
	int framePlaneRGB;
	int stridePlaneRGB;

	/* Flip image */
	int flip;

	/* fullscreen behaviour; see init */
	int fulltype;
	
	/* original image dimensions */
	int width, height;

	/* destination dimensions */
	int dstwidth, dstheight;

	/* Draw image at coordinate y on the SDL surfaces */
	int y;

	/* The image is displayed between those y coordinates in priv->surface */
	int y_screen_top, y_screen_bottom;

	/* 1 if the OSD has changed otherwise 0 */
	int osd_has_changed;

	/* source image format (YUV/RGB/...) */
	uint32_t format;
	int surfwidth, surfheight;

	SDL_Rect dirty_off_frame[2];
} sdl_priv;

static int setup_surfaces(void);
static void set_video_mode(int width, int height, int bpp, uint32_t sdlflags);
/** libvo Plugin functions **/

/**
 * draw_alpha is used for osd and subtitle display.
 *
 **/

static void draw_alpha(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride)
{
}


/**
 * Take a null-terminated array of pointers, and find the last element.
 *
 *    params : array == array of which we want to find the last element.
 *   returns : index of last NON-NULL element.
 **/


/**
 * Open and prepare SDL output.
 *
 *    params : *plugin ==
 *             *name ==
 *   returns : 0 on success, -1 on failure
 **/

static int sdl_open (void *plugin, void *name)
{
	struct sdl_priv_s *priv = &sdl_priv;
	const SDL_VideoInfo *vidInfo = NULL;

	/* Setup Keyrepeats (500/30 are defaults) */
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, 100 /*SDL_DEFAULT_REPEAT_INTERVAL*/);

	/* get information about the graphics adapter */
	vidInfo = SDL_GetVideoInfo ();

	/* collect all fullscreen & hardware modes available */
	if (!(priv->fullmodes = SDL_ListModes (vidInfo->vfmt, priv->sdlfullflags))) {

		/* non hardware accelerated fullscreen modes */
		priv->sdlfullflags &= ~SDL_HWSURFACE;
 		priv->fullmodes = SDL_ListModes (vidInfo->vfmt, priv->sdlfullflags);
	}

	/* test for normal resizeable & windowed hardware accellerated surfaces */
	if (!SDL_ListModes (vidInfo->vfmt, priv->sdlflags)) {

		/* test for NON hardware accelerated resizeable surfaces - poor you.
		 * That's all we have. If this fails there's nothing left.
		 * Theoretically there could be Fullscreenmodes left - we ignore this for now.
		 */
		priv->sdlflags &= ~SDL_HWSURFACE;
		if ((!SDL_ListModes (vidInfo->vfmt, priv->sdlflags)) && (!priv->fullmodes)) {
			mp_msg(MSGT_VO,MSGL_ERR, MSGTR_LIBVO_SDL_CouldntGetAnyAcceptableSDLModeForOutput);
			return -1;
		}
	}


   /* YUV overlays need at least 16-bit color depth, but the
	* display might less. The SDL AAlib target says it can only do
	* 8-bits, for example. So, if the display is less than 16-bits,
	* we'll force the BPP to 16, and pray that SDL can emulate for us.
	*/
	priv->bpp = vidInfo->vfmt->BitsPerPixel;
	return 0;
}


/**
 * Close SDL, Cleanups, Free Memory
 *
 *    params : *plugin
 *   returns : non-zero on success, zero on error.
 **/

static int sdl_close (void)
{
	struct sdl_priv_s *priv = &sdl_priv;
	SDL_Quit();
	return 0;
}

/**
 * Do aspect ratio calculations
 *
 *   params : srcw == sourcewidth
 *            srch == sourceheight
 *            dstw == destinationwidth
 *            dsth == destinationheight
 *
 *  returns : SDL_Rect structure with new x and y, w and h
 **/

/**
 * Sets the specified fullscreen mode.
 *
 *   params : mode == index of the desired fullscreen mode
 *  returns : doesn't return
 **/

/* Set video mode. Not fullscreen */
static void set_video_mode(int width, int height, int bpp, uint32_t sdlflags)
{
	struct sdl_priv_s *priv = &sdl_priv;
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_SetVideoMode(0, 0, 0, SDL_OPENGLES);
	GL_Init();
	GL_InitTexture(width, height);
	glUseProgram ( program );

	//Calculate the correct aspect ratio for the texture coordinates
	//Given the screen size of 1024x768
	float yAspect = (1024.0 / ((float)width/height)) / 768.0;
	float scale_fit[8] =
	{
	  1, yAspect,
	  -1, yAspect,
	  1, -yAspect,
          -1, -yAspect
	};
	
	memcpy( vertexCoords, scale_fit, 8*sizeof(float));
	glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
	glVertexAttribPointer( texCoord, 2, GL_FLOAT, GL_FALSE, 2*sizeof(GLfloat), texCoords );
	glEnableVertexAttribArray( position );    
	glEnableVertexAttribArray( texCoord );
	glBindTexture(GL_TEXTURE_2D, texture);
	
	priv->dstwidth = width;
	priv->dstheight = height;
}


/**
 * Initialize an SDL surface and an SDL YUV overlay.
 *
 *    params : width  == width of video we'll be displaying.
 *             height == height of video we'll be displaying.
 *             fullscreen == want to be fullscreen?
 *             title == Title for window titlebar.
 *   returns : non-zero on success, zero on error.
 **/
static int
config(uint32_t width, uint32_t height, uint32_t d_width, uint32_t d_height, uint32_t flags, char *title, uint32_t format)
//static int sdl_setup (int width, int height)
{
	struct sdl_priv_s *priv = &sdl_priv;
	    d_width = width;
	    d_height = height;

	aspect_save_orig(width,height);
	aspect_save_prescale(d_width ? d_width : width, d_height ? d_height : height);
	priv->width  = width;
	priv->height = height;
	priv->dstwidth  = d_width ? d_width : width;
	priv->dstheight = d_height ? d_height : height;
	priv->windowsize.w = priv->dstwidth;
  	priv->windowsize.h = priv->dstheight;
	
	set_video_mode(priv->dstwidth, priv->dstheight, priv->bpp, priv->sdlflags);
	return 0;
}

static int setup_surfaces(void)
{
}


/**
 * Draw a frame to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the frame.
 *  returns : non-zero on success, zero on error.
 **/

//static int sdl_draw_frame (frame_t *frame)
static int draw_frame(uint8_t *src[])
{
	struct sdl_priv_s *priv = &sdl_priv;
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0, priv->dstwidth,priv->dstheight,GL_RGBA,GL_UNSIGNED_BYTE,src[0]);
	glUniform1i( samplerLoc, 0 );
	glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
	return 0;
}


/**
 * Draw a slice (16 rows of image) to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the slice.
 *  returns : non-zero on error, zero on success.
 **/

static int draw_slice(uint8_t *image[], int stride[], int w,int h,int x,int y)
{
	return 0;
}



/**
 * Checks for SDL keypress and window resize events
 *
 *   params : none
 *  returns : doesn't return
 **/

#include "osdep/keycodes.h"

#define shift_key (event.key.keysym.mod==(KMOD_LSHIFT||KMOD_RSHIFT))

static void check_events (void)
{/*
	struct sdl_priv_s *priv = &sdl_priv;
	SDL_Event event;
	SDLKey keypressed = SDLK_UNKNOWN;
	while ( SDL_PollEvent(&event) ) {
		switch (event.type) {
			case SDL_KEYDOWN:
				printf("A key was pressed!\n\n");
				keypressed = event.key.keysym.sym;
 				mp_msg(MSGT_VO,MSGL_DBG2, "SDL: Key pressed: '%i'\n", keypressed);
				switch(keypressed){
					case SDLK_q: mplayer_put_key('q');break;
					case SDLK_w: mplayer_put_key(KEY_UP);break;
	                            	case SDLK_z: mplayer_put_key(KEY_DOWN);break;
					case SDLK_a: mplayer_put_key(KEY_LEFT);break;
					case SDLK_d: mplayer_put_key(KEY_RIGHT);break;
					case SDLK_c: change_scale();break;
					default:
						mplayer_put_key(keypressed);
				}

				break;
			case SDL_QUIT: mplayer_put_key(KEY_CLOSE_WIN);break;
		}
	}
*/}
#undef shift_key

/* Erase (paint it black) the rectangle specified by x, y, w and h in the surface
   or overlay which is used for OSD
*/
static void draw_osd(void)
{   
}

/* Fill area beginning at 'pixels' with 'color'. 'x_start', 'width' and 'pitch'
 * are given in bytes. 4 bytes at a time.
 */
/* Fill area beginning at 'pixels' with 'color'. 'x_start', 'width' and 'pitch'
 * are given in bytes. 1 byte at a time.
 */
/**
 * Display the surface we have written our data to
 *
 *   params : mode == index of the desired fullscreen mode
 *  returns : doesn't return
 **/

static void flip_page (void)
{
	SDL_GL_SwapBuffers();
	glClear(GL_COLOR_BUFFER_BIT);
}

static int
query_format(uint32_t format)
{
	if (format != IMGFMT_RGB32) return 0;	
	return VFCAP_CSP_SUPPORTED | VFCAP_OSD | VFCAP_FLIP;
}


static void
uninit(void)
{
	sdl_close();
 	mp_msg(MSGT_VO,MSGL_DBG3, "SDL: Closed Plugin\n");

}

static int preinit(const char *arg)
{
	struct sdl_priv_s *priv = &sdl_priv;
	char * sdl_driver = NULL;
	SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE);
	SDL_VideoDriverName(priv->driver, 8);
	mp_msg(MSGT_VO,MSGL_INFO, MSGTR_LIBVO_SDL_UsingDriver, priv->driver);
	return 0;
}

static uint32_t get_image(mp_image_t *mpi)
{
/*    struct sdl_priv_s *priv = &sdl_priv;
	    mpi->planes[0] = (uint8_t *)priv->rgbsurface->pixels + priv->y*priv->rgbsurface->pitch;
	    mpi->stride[0] = priv->rgbsurface->pitch;
	    mpi->flags|=MP_IMGFLAG_DIRECT;*/
	    return VO_TRUE;
}

static int control(uint32_t request, void *data)
{
  struct sdl_priv_s *priv = &sdl_priv;
  switch (request) {
  case VOCTRL_GET_IMAGE:
	  return get_image(data);
  case VOCTRL_QUERY_FORMAT:
	return query_format(*((uint32_t*)data));
  case VOCTRL_FULLSCREEN:
//      set_video_mode(priv->windowsize.w, priv->windowsize.h, priv->bpp, priv->sdlflags);
	return VO_TRUE;
  }

  return VO_NOTIMPL;
}
