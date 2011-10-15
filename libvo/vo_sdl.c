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

GLuint program;

//Texture stuff
static GLuint texture;  //The texture
static GLuint yuvTextures[3];
GLint position;         //Texture position
GLint texCoord;         //Texture coordinates
GLint samplerLoc;       //Used to get a ref to the sampler2D texture in the shader
GLint yLoc;
GLint uLoc;
GLint vLoc;

//Geometry and texture coordinates
float vertexCoords[8];
GLushort indices[] = { 0, 1, 2, 1, 2, 3 };

float texCoords[] =
{
	1.0, 0.0,
	0.0, 0.0,
	1.0, 1.0,
	0.0, 1.0
};

//For subtitles
static unsigned char *ImageData=NULL;
static uint32_t image_width;
static uint32_t image_height;

static const vo_info_t info =
{
	"SDL YUV/RGB/BGR renderer (SDL v1.1.7+ only!)",
	"sdl",
	"Ryan C. Gordon <icculus@lokigames.com>, Felix Buenemann <atmosfear@users.sourceforge.net>",
	""
};

const LIBVO_EXTERN(sdl)

static int sdl_close (void);

GLuint LoadShaderProgram(const char *p1, const char *p2)
{
    GLint status;
    
	GLuint vshader;
	GLuint fshader;
	GLuint program;

	vshader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vshader,1,&p1,NULL);
	glCompileShader(vshader);

	fshader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fshader,1,&p2,NULL);
	glCompileShader(fshader);
    
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &status);
    if (status == 0){
        printf("Vertex shader failed to comile!\n");
        sdl_close();
        return -1;
    }
    
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &status);
    if (status == 0){
        printf("Fragment shader failed to comile!\n");
        sdl_close();
        return -1;
    }

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
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );//black background
	glDisable(GL_DEPTH_TEST);
	glDepthFunc( GL_ALWAYS );
	glDisable(GL_CULL_FACE);

    //TODO: It would be nice to load these from a file
    
    ///A very basic vertex shader for our textured quad
	GLbyte vShaderStr[] =  
	    "attribute vec4 a_position;   \n"
	    "attribute vec2 a_texCoord;   \n"
	    "varying vec2 v_texCoord;     \n"
	    "void main()                  \n"
	    "{                            \n"
	    "   gl_Position = a_position; \n"
	    "   v_texCoord = a_texCoord;  \n"
	    "}                            \n";

    //A basic fragment shader for texturing
	GLbyte fShaderStr[] =  
	    "precision lowp float;                            	 \n"
	    "varying vec2 v_texCoord;                            \n"
	    "uniform sampler2D tex0;                             \n"
	    "void main()                                         \n"
	    "{                                                   \n"
	    "  gl_FragColor = texture2D( tex0, v_texCoord );     \n"
	    "}                                                   \n";

    //A test shader to help with debug
    GLbyte testShader[] =
        "void main (void)                              \n"
        "{                                             \n"
        "    gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);  \n"
        "}                                              ";
       
	//A fragment shader for converting from YUV to RGB
	GLbyte YUVtoRGBShader[] = 
	"precision lowp float;\n"
	"uniform sampler2D tex0, tex1, tex2;		\n"
	"varying vec2 v_texCoord;					\n"

	"void main(void) {							\n"
	"  float r, g, b, y, u, v;					\n"
	"  y = texture2D(tex0, v_texCoord).x;		\n"
	"  u = texture2D(tex1, v_texCoord).x;		\n"
	"  v = texture2D(tex2, v_texCoord).x;		\n"

	"  y = 1.1643 * (y - 0.0625);				\n"
	"  u = u - 0.5;								\n"
	"  v = v - 0.5;								\n"

	"  r = y + 1.5958 * v;						\n"
	"  g = y - 0.39173 * u - 0.81290 * v;		\n"
	"  b = y + 2.017 * u;						\n"

	"  gl_FragColor = vec4(r, g, b, 1.0);		\n"
	"}											\n";
 
	program = LoadShaderProgram(( char *)vShaderStr, (char *)fShaderStr);

	position = glGetAttribLocation ( program, "a_position" );
	texCoord = glGetAttribLocation ( program, "a_texCoord" );

	yLoc = glGetUniformLocation ( program, "tex0" );
//	uLoc = glGetUniformLocation ( program, "tex1" );
//	vLoc = glGetUniformLocation ( program, "tex2" );
}
void GL_InitTexture(int width, int height)
{
	
	glGenTextures(1, &texture);
    
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	
	/*
	glGenTextures(3, yuvTextures);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, yuvTextures[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
	
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, yuvTextures[1]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );   
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );   
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width>>1, height>>1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
	
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, yuvTextures[2]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width>>1, height>>1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
	*/
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

static void (*draw_alpha_fnc)
   (int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride);

static void draw_alpha_32(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride){
   struct sdl_priv_s *priv = &sdl_priv;
   vo_draw_alpha_rgb32(w,h,src,srca,stride,ImageData+4*(y0*priv->width+x0),4*priv->width);
}

static void draw_alpha_24(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride){
   struct sdl_priv_s *priv = &sdl_priv;
   vo_draw_alpha_rgb24(w,h,src,srca,stride,ImageData+3*(y0*priv->width+x0),3*priv->width);
}

static void draw_alpha_16(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride){
   struct sdl_priv_s *priv = &sdl_priv;
   vo_draw_alpha_rgb16(w,h,src,srca,stride,ImageData+2*(y0*priv->width+x0),2*priv->width);
}

static void draw_alpha_15(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride){
   struct sdl_priv_s *priv = &sdl_priv;
   vo_draw_alpha_rgb15(w,h,src,srca,stride,ImageData+2*(y0*priv->width+x0),2*priv->width);
}

static void draw_alpha_null(int x0,int y0, int w,int h, unsigned char* src, unsigned char *srca, int stride){
}

static int setup_surfaces(void);
static void set_video_mode(int width, int height, int bpp, uint32_t sdlflags);
/** libvo Plugin functions **/

/**
 * Open and prepare SDL output.
 *
 *    params : *plugin ==
 *             *name ==
 *   returns : 0 on success, -1 on failure
 **/
static int sdl_open (void *plugin, void *name)
{
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
	glDeleteTextures(3, yuvTextures);
	glDeleteTextures(1, &texture);
	struct sdl_priv_s *priv = &sdl_priv;   
	SDL_Quit();
	return 0;
}

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
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_SetVideoMode(0, 0, 0, SDL_OPENGLES);
	
    GL_Init();
	glUseProgram (program);
	GL_InitTexture(width, height);
	
	//Calculate the correct aspect ratio for the vertices iven the screen size of 1024x768
	float yAspect = (1024.0 / ((float)width/height)) / 768.0;
	float scale_fit[8] =
	{
        1, yAspect,
        -1, yAspect,
        1, -yAspect,
        -1, -yAspect
	};
	
    //Copy the scale values to the vertex variable
	memcpy( vertexCoords, scale_fit, 8*sizeof(float));
    
    //Bind the position and texCoord to the vertex shader variables
	glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
	glVertexAttribPointer( texCoord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), texCoords );
    
    //Bind the texture
	glUniform1i(yLoc, 0);
	//glUniform1i(uLoc, 1);
	//glUniform1i(vLoc, 2);

    glBindTexture(GL_TEXTURE_2D, texture);
    
    //Enable the bound attribute arrays so they'll be used when rendering
	glEnableVertexAttribArray( position );    
	glEnableVertexAttribArray( texCoord );
    
    //Clear the color buffer
    glClear(GL_COLOR_BUFFER_BIT);
	
	priv->dstwidth = width;
	priv->dstheight = height;
}

static int
config(uint32_t width, uint32_t height, uint32_t d_width, uint32_t d_height, uint32_t flags, char *title, uint32_t format)
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
	draw_alpha_fnc=draw_alpha_null;

	//For the touchpad this will always be 32
	/*
	switch(priv->bpp) {
	case 15:
		draw_alpha_fnc=draw_alpha_15; break;
	case 16:
		draw_alpha_fnc=draw_alpha_16; break;
	case 24:
		draw_alpha_fnc=draw_alpha_24; break;
	case 32:
		draw_alpha_fnc=draw_alpha_32; break;
  	}
	return 0;
	*/
	draw_alpha_fnc=draw_alpha_32;
}

/**
 * Draw a frame to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the frame.
 *  returns : non-zero on success, zero on error.
 **/

static int draw_frame(uint8_t *src[])
{
	struct sdl_priv_s *priv = &sdl_priv;
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, priv->dstwidth,priv->dstheight, GL_RGBA,GL_UNSIGNED_BYTE, src[0]);
	
	glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
	
	//For the OSD
	ImageData=(unsigned char *)src[0];
	
	//SDL_GL_SwapBuffers();
	return 0;
}


/**
 * Draw a slice (16 rows of image) to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the slice.
 *  returns : non-zero on error, zero on success.
 **/

static int draw_slice(uint8_t *src[], int stride[], int w, int h, int x, int y)
{
	//printf("draw_slice: strideY: %d strideU: %d strideV: %d x: %d y: %d w: %d h: %d\n", stride[0], stride[1], //stride[2], x, y, w, h);

	uint8_t *yptr = src[0], *uptr = src[1], *vptr = src[2];
    
    //printf("draw_slice: x: %d y: %d w: %d h: %d\n", x, y, w, h);
    //Select the texture to work with
    //Overwrite the uniform variable in the fragment shader to reset the texture
    //Texture map the specified slice
 	glActiveTexture(GL_TEXTURE0);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride[0], h, GL_LUMINANCE, GL_UNSIGNED_BYTE, yptr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, stride[0], h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, yptr);
	
    //Adjust w/h to handle the U and V texture sizes
    w=w>>1;h=h>>1;

	glActiveTexture(GL_TEXTURE1);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride[1], h, GL_LUMINANCE, GL_UNSIGNED_BYTE, uptr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, stride[1], h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, uptr);

	glActiveTexture(GL_TEXTURE2);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride[2], h, GL_LUMINANCE, GL_UNSIGNED_BYTE, vptr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, stride[2], h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, vptr);
	
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);	
    SDL_GL_SwapBuffers();

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
{
/*
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

static void draw_osd(void)
{
	if (ImageData){
		struct sdl_priv_s *priv = &sdl_priv;
		vo_draw_text(priv->width, priv->height, draw_alpha_fnc); 
        
		//TODO: I think this is bad, I shouldn't have to update the entire frame to draw the OSD
		//		I should probably pass in another texture to use as the OSD drawing surface, this may
		//		mean I switch to using vo_draw_text_ext
		//		However, this does work for now, albeit slowly
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, priv->dstwidth,priv->dstheight, GL_RGBA, GL_UNSIGNED_BYTE, ImageData);
	
        glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
	}
}

static void flip_page (void)
{
    //TODO when is this called?
	SDL_GL_SwapBuffers();
	glClear(GL_COLOR_BUFFER_BIT);
}

static int
query_format(uint32_t format)
{

	//return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW | VFCAP_OSD |
      //     VFCAP_HWSCALE_UP | VFCAP_HWSCALE_DOWN | VFCAP_ACCEPT_STRIDE;
	
 	switch(format) {
    	case IMGFMT_RGB32:
        	return VFCAP_CSP_SUPPORTED | VFCAP_OSD | VFCAP_CSP_SUPPORTED_BY_HW;
	}

	return VFCAP_OSD | VFCAP_ACCEPT_STRIDE;
}


static void uninit(void)
{
	sdl_close();
 	mp_msg(MSGT_VO,MSGL_DBG3, "SDL: Closed Plugin\n");

}

static int preinit(const char *arg)
{
	struct sdl_priv_s *priv = &sdl_priv;
	//char * sdl_driver = NULL;
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
