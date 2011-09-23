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

/* MONITOR_ASPECT MUST BE FLOAT */
#define MONITOR_ASPECT 4.0/3.0
#define shift_key (event.key.keysym.mod==(KMOD_LSHIFT||KMOD_RSHIFT))

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
#include "mp_fifo.h"

#include <SDL/SDL.h>
#include <SDL/SDL_opengles.h>
#include <SDL/SDL_opengles_ext.h>
#include <PDL.h>

#include "osdep/keycodes.h"

static int scale;
static GLuint texture[3];
GLushort indices[] = { 0, 1, 2, 1, 2, 3 };
GLuint program;
GLint  position;
GLint  texCoord;
GLint sampler;
GLint loc_tex_y;
GLint loc_tex_u;
GLint loc_tex_v;
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
    0.0, 0.0,
    0.0, 1.0,
    1.0, 0.0,
    1.0, 1.0
};

#ifdef CONFIG_X11
#include <X11/Xlib.h>
#include "x11_common.h"
#endif

#include "subopt-helper.h"

static const vo_info_t info =
{
	"OPENGL_ES YUV driver for Webos",
	"sdlopengl",
	"Chomper",
	"thbhdforever@gmail.com"`
};


const LIBVO_EXTERN(sdl)

#include "sdl_common.h"
//#include <SDL/SDL_syswm.h>

/** Private SDL Data structure **/

static struct sdl_priv_s {
	char driver[8];
 	int width, height;
 	int dstwidth, dstheight;
} sdl_priv;

static int change_scale(void)
{
	switch (scale) {
		case 0:
			memcpy( vertexCoords, scale_n, 8*sizeof(float));
			glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
			scale = 2;
			break;
		case 1:
			memcpy( vertexCoords, scale_1, 8*sizeof(float));
			glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
			scale = 0;
			break;
		case 2:
			memcpy( vertexCoords, scale_2, 8*sizeof(float));
			glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
			scale = 1;
			break;
	}
	glClear(GL_COLOR_BUFFER_BIT);
}

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
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );  
    glDisable(GL_DEPTH_TEST);
    glDepthFunc( GL_ALWAYS );
    glDisable(GL_STENCIL_TEST);
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
	GLbyte YUVSharder[] =
		"precision mediump float;								\n"
		"varying vec2 v_texCoord;								\n"
		"uniform sampler2D Ytex, Utex, Vtex;					\n"
		"void main(void) 										\n"
		"{														\n"
		"float y,u,v;											\n"
		"float r,g,b;											\n"
		"y = 1.1643 * (texture2D(Ytex, v_texCoord).r - 0.0625);	\n"
		"u = texture2D(Utex, v_texCoord).r - 0.5;				\n"
		"v = texture2D(Vtex, v_texCoord).r - 0.5;				\n"
		"r = y  1.5958 * v;									\n"
		"g = y - 0.39173 * u - 0.81290 * v;						\n"
		"b = y  2.017 * u;										\n"
		"gl_FragColor=vec4(r, g, b, 1.0);						\n"
		"}														\n"	;
		
	program = LoadShaderProgram ( ( char *)vShaderStr, (char *)YUVSharder);
    
    position = glGetAttribLocation ( program, "a_position");
    
    texCoord = glGetAttribLocation ( program, "a_texCoord");
    
	loc_tex_y = glGetUniformLocation (program, "Ytex");
	loc_tex_u = glGetUniformLocation (program, "Utex");
	loc_tex_v = glGetUniformLocation (program, "Vtex");
}

void GL_InitTexture(int width, int height)
{
	glGenTextures(3, texture);

    glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
	
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );   
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );   
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width>>1, height>>1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
		
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width>>1, height>>1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,0);
}

static void set_video_mode(int width, int height);

static int sdl_close (void)
{
	SDL_Quit();
	return 0;
}

/* Set video mode. Not fullscreen */
static void set_video_mode(int width, int height)
{
	struct sdl_priv_s *priv = &sdl_priv;
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION,2);
	PDL_Init(0);
	SDL_SetVideoMode(width, height, 0, SDL_OPENGLES);
	GL_Init();
	GL_InitTexture(width,height);
	glUseProgram (program);
	memcpy(vertexCoords, scale_n, 8*sizeof(float));
	glVertexAttribPointer( position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
	glVertexAttribPointer( texCoord, 2, GL_FLOAT, GL_FALSE, 2*sizeof(GLfloat), texCoords );
	glEnableVertexAttribArray( position );    
	glEnableVertexAttribArray( texCoord );
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
	priv->dstwidth  = d_width ? d_width : width;
	priv->dstheight = d_height ? d_height : height;

	set_video_mode(priv->dstwidth, priv->dstheight);
	return 0;
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
	glBindTexture(GL_TEXTURE_2D,0);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0, priv->dstwidth,priv->dstheight,GL_RGBA,GL_UNSIGNED_BYTE,src[0]);
	glUniform1i(sampler, 0);
	
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
	SDL_GL_SwapBuffers();
//	printf("use time %d ms\n",SDL_GetTicks()-start); //test the one frame use time...
//	start = SDL_GetTicks();
	return 0;
}


/**
 * Draw a slice (16 rows of image) to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the slice.
 *  returns : non-zero on error, zero on success.
 **/

//static uint32_t draw_slice(uint8_t *src[], uint32_t slice_num)
static int draw_slice(uint8_t *src[], int stride[], int w,int h,int x,int y)
{	
	glBindTexture(GL_TEXTURE_2D,0);
	glUniform1i(loc_tex_y, 0);
//	glTexSubImage2D(GL_TEXTURE_2D,0, 0, 0, w, h,GL_LUMINANCE,GL_UNSIGNED_BYTE,src[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,src[0]);//it is faster on this device ...
	w=w>>1;h=h>>1;

	glBindTexture(GL_TEXTURE_2D,1);
	glUniform1i(loc_tex_u, 1);
//	glTexSubImage2D(GL_TEXTURE_2D,0, 0, 0, w, h,GL_LUMINANCE,GL_UNSIGNED_BYTE,src[1]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,src[1]);

	glBindTexture(GL_TEXTURE_2D,2);
	glUniform1i(loc_tex_v, 2);
//	glTexSubImage2D(GL_TEXTURE_2D,0, 0, 0, w, h,GL_LUMINANCE,GL_UNSIGNED_BYTE,src[2]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,src[2]);
	
	//flip to screen..
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
	SDL_GL_SwapBuffers();
//	printf("use time %d ms\n",SDL_GetTicks()-start); //test the one frame use time...
//	start = SDL_GetTicks();

	return 0;
}



/**
 * Checks for SDL keypress and window resize events
 *
 *   params : none
 *  returns : doesn't return
 **/

static void check_events (void)
{
	SDL_Event event;
	SDLKey keypressed = SDLK_UNKNOWN;

	/* Poll the waiting SDL Events */
	while ( SDL_PollEvent(&event) ) {
		switch (event.type) {
			case SDL_KEYDOWN:
				keypressed = event.key.keysym.sym;
 				mp_msg(MSGT_VO,MSGL_DBG2, "SDL: Key pressed: '%i'\n", keypressed);

				switch(keypressed){
					case SDLK_q: mplayer_put_key('q');
						PDL_Quit();
					break;
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
}

#undef shift_key

static void draw_osd(void)
{
}

/* Fill area beginning at 'pixels' with 'color'. 'x_start', 'width' and 'pitch'
 * are given in bytes. 4 bytes at a time.
 */
static void erase_area_4(int x_start, int width, int height, int pitch, uint32_t color, uint8_t* pixels)
{
}

/**
 * Display the surface we have written our data to
 *
 *   params : mode == index of the desired fullscreen mode
 *  returns : doesn't return
 **/

static void flip_page (void)
{
}

static int
query_format(uint32_t format)
{
	if (format != IMGFMT_YV12) return 0;//just use YV12..
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
    SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE);
    SDL_VideoDriverName(priv->driver, 8);
    mp_msg(MSGT_VO,MSGL_INFO, MSGTR_LIBVO_SDL_UsingDriver, priv->driver);

    return 0;
}

static uint32_t get_image(mp_image_t *mpi)
{
}

static int control(uint32_t request, void *data)
{
  switch (request) {
  case VOCTRL_GET_IMAGE:
      return get_image(data);
  case VOCTRL_QUERY_FORMAT:
    return query_format(*((uint32_t*)data));
  case VOCTRL_FULLSCREEN:
    return VO_TRUE;
  }

  return VO_NOTIMPL;
}
