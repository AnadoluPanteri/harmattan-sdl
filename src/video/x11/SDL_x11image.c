/*
 * SDL X11/EGL/GLES2 backend
 * Copyright (C) 2012 Ville Syrjälä <syrjala@sci.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * TODO try to eliminate shadow buffer in case preserved flips aren't needed?
 */
#include "SDL_config.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "SDL_endian.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_pixels_c.h"
#include "SDL_x11image_c.h"

static PFNEGLLOCKSURFACEKHRPROC eglLockSurfaceKHR;
static PFNEGLUNLOCKSURFACEKHRPROC eglUnlockSurfaceKHR;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

#if 0
#define TRACE() printf("%s/%d\n", __func__, __LINE__)
#define DPRINTF(fmt, ...) printf("%s/%d: " fmt, __func__, __LINE__, __VA_ARGS__)
#else
#define TRACE() do {} while (0)
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define ERROR() fprintf(stderr, "ERROR: %s/%d\n", __func__, __LINE__)

struct private_hwdata {
	Pixmap pixmap;
	EGLSurface surface;
	EGLImageKHR image;
	GLuint tex;
	GLuint fbo;
	SDL_VideoDevice *this;
	int ref;
	struct private_hwdata *finish_hwdata;
};

static struct private_hwdata screen_hwdata;

static void bind_framebuffer(struct SDL_PrivateVideoData *hidden, struct private_hwdata *hwdata);
static void bind_texture(struct SDL_PrivateVideoData *hidden, struct private_hwdata *hwdata);
static void set_finish_hwdata(struct private_hwdata *hwdata, struct private_hwdata *finish_hwdata);

static void hwdata_free(struct private_hwdata *hwdata)
{
	struct SDL_VideoDevice *this = hwdata->this;
	struct SDL_PrivateVideoData *hidden = this->hidden;

	TRACE();

	if (glGetError())
		ERROR();

	if (hidden->gl.tex_hwdata == hwdata)
		bind_texture(hidden, NULL);
	if (hidden->gl.fbo_hwdata == hwdata)
		bind_framebuffer(hidden, NULL);

	set_finish_hwdata(hwdata, NULL);

	if (hwdata->fbo) {
		glDeleteFramebuffers(1, &hwdata->fbo);
		hwdata->fbo = 0;
	}

	if (hwdata->tex) {
		glDeleteTextures(1, &hwdata->tex);
		hwdata->tex = 0;
	}

	if (hwdata->image != EGL_NO_IMAGE_KHR) {
		eglDestroyImageKHR(hidden->egl.dpy, hwdata->image);
		hwdata->image = EGL_NO_IMAGE_KHR;
	}

	if (hwdata->surface != EGL_NO_SURFACE)
		eglDestroySurface(hidden->egl.dpy, hwdata->surface);

	if (hwdata->pixmap != None)
		XFreePixmap(SDL_Display, hwdata->pixmap);

	SDL_free(hwdata);

	if (glGetError())
		ERROR();

	TRACE();
}

static struct private_hwdata *hwdata_alloc(SDL_VideoDevice *this)
{
	struct private_hwdata *hwdata;

	hwdata = SDL_calloc(1, sizeof *hwdata);
	if (!hwdata)
		return NULL;

	hwdata->this = this;
	hwdata->ref = 1;

	return hwdata;
}

static struct private_hwdata *hwdata_ref(struct private_hwdata *hwdata)
{
	if (!hwdata || hwdata == &screen_hwdata)
		return hwdata;

	hwdata->ref++;

	return hwdata;
}

static void hwdata_unref(struct private_hwdata *hwdata)
{
	if (!hwdata || hwdata == &screen_hwdata)
		return;

	if (--hwdata->ref == 0)
		hwdata_free(hwdata);
}

static void set_finish_hwdata(struct private_hwdata *hwdata,
			      struct private_hwdata *finish_hwdata)
{
	struct private_hwdata *old_finish_hwdata = hwdata->finish_hwdata;
	hwdata->finish_hwdata = hwdata_ref(finish_hwdata);
	hwdata_unref(old_finish_hwdata);
}

struct rgba_color {
	GLfloat r, g, b, a;
};

static void pixel_to_color(const SDL_PixelFormat *format,
			   Uint32 pixel, struct rgba_color *color)
{
	switch (format->BitsPerPixel) {
	case 12:
		color->a = ((pixel >> 12) & 0xf) / 15.0f;
		color->r = ((pixel >>  8) & 0xf) / 15.0f;
		color->g = ((pixel >>  4) & 0xf) / 15.0f;
		color->b = ((pixel >>  0) & 0xf) / 15.0f;
		break;
	case 15:
		color->a = ((pixel >> 15) & 0x01) /  1.0f;
		color->r = ((pixel >> 10) & 0x1f) / 31.0f;
		color->g = ((pixel >>  5) & 0x1f) / 31.0f;
		color->b = ((pixel >>  0) & 0x1f) / 31.0f;
		break;
	case 16:
		color->a = 1.0f;
		color->r = ((pixel >> 11) & 0x1f) / 31.0f;
		color->g = ((pixel >>  5) & 0x3f) / 63.0f;
		color->b = ((pixel >>  0) & 0x1f) / 31.0f;
		break;
	case 24:
		color->a = 1.0f;
		color->r = ((pixel >> 16) & 0xff) / 255.0f;
		color->g = ((pixel >>  8) & 0xff) / 255.0f;
		color->b = ((pixel >>  0) & 0xff) / 255.0f;
		break;
	case 32:
		color->a = ((pixel >> 24) & 0xff) / 255.0f;
		color->r = ((pixel >> 16) & 0xff) / 255.0f;
		color->g = ((pixel >>  8) & 0xff) / 255.0f;
		color->b = ((pixel >>  0) & 0xff) / 255.0f;
		break;
	}
}

static GLuint compile_shaders(const char *vs_source,
			      const char *fs_source)
{
	GLuint vs, fs, prog;
	GLint status = 0;

	TRACE();

	vs = glCreateShader(GL_VERTEX_SHADER);
	if (!vs)
		goto out;
	glShaderSource(vs, 1, &vs_source, NULL);
	glCompileShader(vs);
	TRACE();
	glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
	if (!status)
		goto delete_vs;

	TRACE();

	fs = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fs)
		goto delete_vs;
	glShaderSource(fs, 1, &fs_source, NULL);
	glCompileShader(fs);
	TRACE();
	glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
	if (!status)
		goto delete_fs;

	TRACE();

	prog = glCreateProgram();
	if (!prog)
		goto delete_fs;
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	TRACE();
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (!status)
		goto delete_prog;

	TRACE();

	/* prog holds a reference to these now */
	glDeleteShader(vs);
	glDeleteShader(fs);

	TRACE();

	return prog;

 delete_prog:
	glDeleteProgram(prog);
 delete_fs:
	glDeleteShader(fs);
 delete_vs:
	glDeleteShader(vs);
 out:
	ERROR();
	return 0;
}

static int compile_fill_shaders(struct SDL_PrivateVideoData *hidden)
{
	static const char *vs =
		"precision lowp float;\n"
		"attribute vec2 in_position;\n"
		"\n"
		"void main()\n"
		"{\n"
		" gl_Position = vec4(in_position, 0.0, 1.0);\n"
		"}\n";
	static const char *fs =
		"precision lowp float;\n"
		"uniform vec4 color;\n"
		"\n"
		"void main()\n"
		"{\n"
		" gl_FragColor = color;\n"
		"}\n";

	TRACE();

	if (!hidden->gl.prog_fill)
		hidden->gl.prog_fill = compile_shaders(vs, fs);

	return hidden->gl.prog_fill ? 0 : -1;
}

static int compile_blit_shaders(struct SDL_PrivateVideoData *hidden)
{
	static const char *vs =
		"precision lowp float;\n"
		"attribute vec2 in_position;\n"
		"attribute vec2 in_texcoord;\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		" gl_Position = vec4(in_position, 0.0, 1.0);\n"
		" texcoord = in_texcoord;\n"
		"}\n";
	static const char *fs =
		"precision lowp float;\n"
		"uniform sampler2D tex;\n"
		"uniform vec4 color;\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		" vec4 texel = texture2D(tex, texcoord);\n"
		" gl_FragColor = vec4(texel.rgb, texel.a * color.a);\n"
		"}\n";

	TRACE();

	if (!hidden->gl.prog_blit)
		hidden->gl.prog_blit = compile_shaders(vs, fs);

	return hidden->gl.prog_blit ? 0 : -1;
}

static int compile_ckey_shaders(struct SDL_PrivateVideoData *hidden)
{
	static const char *vs =
		"precision lowp float;\n"
		"attribute vec2 in_position;\n"
		"attribute vec2 in_texcoord;\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		" gl_Position = vec4(in_position, 0.0, 1.0);\n"
		" texcoord = in_texcoord;\n"
		"}\n";
	static const char *fs =
		"precision lowp float;\n"
		"uniform sampler2D tex;\n"
		"uniform vec4 color;\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		" vec4 texel = texture2D(tex, texcoord);\n"
		" if (texel.rgb == color.rgb)\n"
		"  discard;\n"
		" gl_FragColor = vec4(texel.rgb, texel.a * color.a);\n"
		"}\n";

	TRACE();

	if (!hidden->gl.prog_ckey)
		hidden->gl.prog_ckey = compile_shaders(vs, fs);

	return hidden->gl.prog_ckey ? 0 : -1;
}

static void draw_quad(const GLfloat *verts,
		      const GLfloat *texcoords,
		      const struct rgba_color *color,
		      GLuint prog)
{
	GLint attr;

	TRACE();

	glUseProgram(prog);

	attr = glGetAttribLocation(prog, "in_position");
	glVertexAttribPointer(attr, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(attr);

	if (texcoords) {
		attr = glGetAttribLocation(prog, "in_texcoord");
		glVertexAttribPointer(attr, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
		glEnableVertexAttribArray(attr);
	}

	attr = glGetUniformLocation(prog, "color");
	glUniform4f(attr, color->r, color->g, color->b, color->a);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void bind_texture(struct SDL_PrivateVideoData *hidden, struct private_hwdata *hwdata)
{
	if (hidden->gl.tex_hwdata == hwdata)
		return;

	TRACE();

	glBindTexture(GL_TEXTURE_2D, hwdata ? hwdata->tex : 0);
	hidden->gl.tex_hwdata = hwdata;

	TRACE();
}

static void bind_framebuffer(struct SDL_PrivateVideoData *hidden, struct private_hwdata *hwdata)
{
	if (hwdata == &screen_hwdata)
		hwdata = NULL;

	if (hidden->gl.fbo_hwdata == hwdata)
		return;

	TRACE();

	glBindFramebuffer(GL_FRAMEBUFFER, hwdata ? hwdata->fbo : 0);
	hidden->gl.fbo_hwdata = hwdata;

	TRACE();
}

#define ARRAY_SIZE(a) (int)(sizeof(a)/sizeof((a)[0]))
#define DEF_ATTR(ATTR) { .name = #ATTR, .attr = ATTR, }

static void print_configs(struct SDL_PrivateVideoData *hidden)
{
	XWindowAttributes attr;
	EGLConfig config[64];
	int num_config;
	static struct {
		const char *name;
		EGLint attr;
	} attrs[] = {
		DEF_ATTR(EGL_BUFFER_SIZE),
		DEF_ATTR(EGL_ALPHA_SIZE),
		DEF_ATTR(EGL_BLUE_SIZE),
		DEF_ATTR(EGL_GREEN_SIZE),
		DEF_ATTR(EGL_RED_SIZE),
		DEF_ATTR(EGL_DEPTH_SIZE),
		DEF_ATTR(EGL_STENCIL_SIZE),
		DEF_ATTR(EGL_CONFIG_CAVEAT),
		DEF_ATTR(EGL_CONFIG_ID),
		DEF_ATTR(EGL_LEVEL),
		DEF_ATTR(EGL_MAX_PBUFFER_HEIGHT),
		DEF_ATTR(EGL_MAX_PBUFFER_PIXELS),
		DEF_ATTR(EGL_MAX_PBUFFER_WIDTH),
		DEF_ATTR(EGL_NATIVE_RENDERABLE),
		DEF_ATTR(EGL_NATIVE_VISUAL_ID),
		DEF_ATTR(EGL_NATIVE_VISUAL_TYPE),
		DEF_ATTR(EGL_SAMPLES),
		DEF_ATTR(EGL_SAMPLE_BUFFERS),
		DEF_ATTR(EGL_SURFACE_TYPE),
		DEF_ATTR(EGL_TRANSPARENT_TYPE),
		DEF_ATTR(EGL_TRANSPARENT_BLUE_VALUE),
		DEF_ATTR(EGL_TRANSPARENT_GREEN_VALUE),
		DEF_ATTR(EGL_TRANSPARENT_RED_VALUE),
		DEF_ATTR(EGL_BIND_TO_TEXTURE_RGB),
		DEF_ATTR(EGL_BIND_TO_TEXTURE_RGBA),
		DEF_ATTR(EGL_MIN_SWAP_INTERVAL),
		DEF_ATTR(EGL_MAX_SWAP_INTERVAL),
		DEF_ATTR(EGL_LUMINANCE_SIZE),
		DEF_ATTR(EGL_ALPHA_MASK_SIZE),
		DEF_ATTR(EGL_COLOR_BUFFER_TYPE),
		DEF_ATTR(EGL_RENDERABLE_TYPE),
		DEF_ATTR(EGL_MATCH_NATIVE_PIXMAP),
		DEF_ATTR(EGL_CONFORMANT),
		DEF_ATTR(EGL_MATCH_FORMAT_KHR),
	};
	int i, j;

	TRACE();

	if (!eglGetConfigs(hidden->egl.dpy, config, ARRAY_SIZE(config), &num_config)) {
		ERROR();
		return;
	}

	for (i = 0; i < num_config; i++) {
		printf("CONFIG %p\n", config[i]);
		for (j = 0; j < ARRAY_SIZE(attrs); j++) {
			EGLint val = 0;
			if (eglGetConfigAttrib(hidden->egl.dpy, config[i], attrs[j].attr, &val))
				printf(" %s = %d / %x\n", attrs[j].name, val, val);
		}
		printf("\n");
	}
	fflush(stdout);
}

static EGLConfig pick_config(SDL_VideoDevice *this)
{
	XWindowAttributes attr;
	EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PIXMAP_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NATIVE_VISUAL_ID, 0,
		EGL_NONE,
	};
	EGLConfig config;
	int num_config;
	struct SDL_PrivateVideoData *hidden = this->hidden;

	TRACE();

	DPRINTF("window = %x\n", SDL_Window);

	if (!XGetWindowAttributes(SDL_Display, SDL_Window, &attr))
		goto out;

	DPRINTF("visual = %x\n", attr.visual->visualid);

	attribs[5] = attr.visual->visualid;

	if (!eglChooseConfig(hidden->egl.dpy, attribs, &config, 1, &num_config))
		goto out;

	DPRINTF("config = %p\n", num_config ? config : NULL);

	if (num_config != 1)
		goto out;

	TRACE();

	return config;

 out:
	ERROR();
	return NULL;
}

static int egl_init(SDL_VideoDevice *this)
{
	static const EGLint attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLint major, minor;
	EGLConfig config;
	struct SDL_PrivateVideoData *hidden = this->hidden;

	TRACE();

	if (hidden->egl.dpy != EGL_NO_DISPLAY)
		return 0;

	hidden->egl.dpy = eglGetDisplay((NativeDisplayType) SDL_Display);
	if (hidden->egl.dpy == EGL_NO_DISPLAY)
		goto out;

	if (!eglInitialize(hidden->egl.dpy, &major, &minor))
		goto terminate;

	//print_configs(this);

	config = pick_config(this);
	if (!config)
		goto terminate;

	hidden->egl.ctx = eglCreateContext(hidden->egl.dpy, config, EGL_NO_CONTEXT, attribs);
	if (hidden->egl.ctx == EGL_NO_CONTEXT)
		goto terminate;

	eglLockSurfaceKHR = (PFNEGLLOCKSURFACEKHRPROC)
		eglGetProcAddress("eglLockSurfaceKHR");
	eglUnlockSurfaceKHR = (PFNEGLUNLOCKSURFACEKHRPROC)
		eglGetProcAddress("eglUnlockSurfaceKHR");

	eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
		eglGetProcAddress("eglCreateImageKHR");
	eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
		eglGetProcAddress("eglDestroyImageKHR");
	glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
		eglGetProcAddress("glEGLImageTargetTexture2DOES");

	TRACE();

	return 0;

 terminate:
	eglTerminate(hidden->egl.dpy);
	hidden->egl.dpy = EGL_NO_DISPLAY;
 out:
	ERROR();
	return -1;
}

static void egl_exit(SDL_VideoDevice *this)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;

	TRACE();

	if (hidden->egl.dpy == EGL_NO_DISPLAY)
		return;

	glDeleteProgram(hidden->gl.prog_ckey);
	glDeleteProgram(hidden->gl.prog_blit);
	glDeleteProgram(hidden->gl.prog_fill);

	eglMakeCurrent(hidden->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	eglDestroyContext(hidden->egl.dpy, hidden->egl.dpy);

	eglTerminate(hidden->egl.dpy);

	hidden->gl.prog_fill = 0;
	hidden->gl.prog_blit = 0;
	hidden->gl.prog_ckey = 0;
	hidden->egl.ctx = EGL_NO_CONTEXT;
	hidden->egl.dpy = EGL_NO_DISPLAY;

	TRACE();
}

static void X11_UpdateRects(SDL_VideoDevice *this, int numrects, SDL_Rect *rects);

static int setup_screen(SDL_VideoDevice *this, SDL_Surface *screen)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *hwdata = screen->hwdata;
	EGLConfig config;
	int r;

	TRACE();

	if (hwdata)
		return 0;

	if (egl_init(this))
		goto out;

	config = pick_config(this);
	if (!config)
		goto free_hwsurface;

	hidden->egl.surface = eglCreateWindowSurface(hidden->egl.dpy, config, (NativeWindowType)SDL_Window, NULL);
	if (hidden->egl.surface == EGL_NO_SURFACE)
		goto free_hwsurface;

	eglMakeCurrent(hidden->egl.dpy, hidden->egl.surface, hidden->egl.surface, hidden->egl.ctx);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (compile_fill_shaders(hidden))
		goto make_current;

	if (compile_blit_shaders(hidden))
		goto free_fill_shaders;

	if (compile_ckey_shaders(hidden))
		goto free_blit_shaders;

	if (X11_AllocHWSurface(this, screen))
		goto exit;

	hidden->gl.dirty = 1;

	//screen->flags |= SDL_FULLSCREEN | SDL_DOUBLEBUF;
	this->UpdateRects = X11_UpdateRects;

	TRACE();

	return 0;

 free_blit_shaders:
	glDeleteProgram(hidden->gl.prog_blit);
 free_fill_shaders:
	glDeleteProgram(hidden->gl.prog_fill);
 make_current:
	eglMakeCurrent(hidden->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
 destroy_surface:
	eglDestroySurface(hidden->egl.dpy, hwdata->surface);
 free_hwsurface:
	X11_FreeHWSurface(this, screen);
 exit:
	egl_exit(this);
 out:
	ERROR();
	return -1;
}

void X11_DestroyImage(SDL_VideoDevice *this, SDL_Surface *screen)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;

	TRACE();

	if (screen && screen->hwdata) {
		hwdata_unref(screen->hwdata);
		screen->hwdata = NULL;
	}

	egl_exit(this);

	TRACE();
}

int X11_ResizeImage(SDL_VideoDevice *this, SDL_Surface *screen, Uint32 flags)
{
	int retval;

	TRACE();

	/* No image when using GL */
	if (flags & (SDL_OPENGL | SDL_OPENGLES)) {
		X11_DestroyImage(this, screen);
		return 0;
	}

	TRACE();

	/* Already have an image? */
	if (screen->hwdata)
		return 0;

	TRACE();

	return setup_screen(this, screen);
}

int X11_AllocHWSurface(SDL_VideoDevice *this, SDL_Surface *surface)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *hwdata;
	XVisualInfo vinfo;
	EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_PIXMAP_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NATIVE_VISUAL_ID, 0,
		EGL_NONE,
	};
	static const EGLint image_attribs[] = {
		EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
		EGL_NONE,
	};

	EGLint num_config;
	EGLConfig config;

	TRACE();

	switch (surface->format->BitsPerPixel) {
	case 16:
		break;
	default:
		goto out;
	}

	if (hidden->egl.dpy == EGL_NO_DISPLAY)
		goto out;

	hwdata = hwdata_alloc(this);
	if (!hwdata)
		goto out;

	if (!XMatchVisualInfo(SDL_Display, SDL_Screen, surface->format->BitsPerPixel, TrueColor, &vinfo))
		goto free_hwdata;

	attribs[5] = vinfo.visualid;

	if (!eglChooseConfig(hidden->egl.dpy, attribs, &config, 1, &num_config))
		goto free_hwdata;

	DPRINTF("%p: w=%d h=%d BitsPerPixel=%d BytesPerPixel=%d\n", surface, surface->w, surface->h,
		surface->format->BitsPerPixel, surface->format->BytesPerPixel);

	hwdata->pixmap = XCreatePixmap(SDL_Display, SDL_Root, surface->w, surface->h, surface->format->BitsPerPixel);
	if (hwdata->pixmap == None)
		goto free_hwdata;

	hwdata->surface = eglCreatePixmapSurface(hidden->egl.dpy, config, (NativePixmapType) hwdata->pixmap, NULL);
	if (hwdata->surface == EGL_NO_SURFACE)
		goto free_hwdata;

	hwdata->image = eglCreateImageKHR(hidden->egl.dpy, EGL_NO_CONTEXT,
					  EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer) hwdata->pixmap, image_attribs);
	if (hwdata->image == EGL_NO_IMAGE_KHR)
		goto free_hwdata;

	glGenFramebuffers(1, &hwdata->fbo);
	glGenTextures(1, &hwdata->tex);

	bind_texture(hidden, hwdata);
	bind_framebuffer(hidden, hwdata);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, hwdata->image);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, hwdata->tex, 0);

	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	set_finish_hwdata(hwdata, hwdata);

	surface->hwdata = hwdata;
	surface->flags |= SDL_HWSURFACE | SDL_PREALLOC;

	TRACE();

	return 0;

 free_hwdata:
	hwdata_unref(hwdata);
 out:
	surface->flags &= ~SDL_HWSURFACE;
	ERROR();
	return -1;
}

void X11_FreeHWSurface(SDL_VideoDevice *this, SDL_Surface *surface)
{
	struct private_hwdata *hwdata = surface->hwdata;

	TRACE();

	DPRINTF("%p: w=%d h=%d BitsPerPixel=%d BytesPerPixel=%d\n", surface, surface->w, surface->h,
		surface->format->BitsPerPixel, surface->format->BytesPerPixel);

	hwdata_unref(hwdata);

	surface->hwdata = NULL;
	surface->flags &= ~SDL_HWSURFACE;

	TRACE();
}

int X11_LockHWSurface(SDL_VideoDevice *this, SDL_Surface *surface)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *hwdata = surface->hwdata;
	static const EGLint attribs[] = {
		EGL_MAP_PRESERVE_PIXELS_KHR, EGL_TRUE,
		EGL_NONE,
	};
	EGLint pointer, pitch;

	TRACE();

	if (hwdata->finish_hwdata) {
		bind_framebuffer(hidden, hwdata->finish_hwdata);
		glFinish();
		set_finish_hwdata(hwdata, NULL);
	}

	if (!eglLockSurfaceKHR(hidden->egl.dpy, hwdata->surface, attribs))
		goto out;

	if (!eglQuerySurface(hidden->egl.dpy, hwdata->surface, EGL_BITMAP_POINTER_KHR, &pointer))
		goto unlock;

	if (!eglQuerySurface(hidden->egl.dpy, hwdata->surface, EGL_BITMAP_PITCH_KHR, &pitch))
		goto unlock;

	surface->pixels = (void *) pointer;
	surface->pitch = pitch;

	/* Can't know whether it's a read or write :( */
	if (surface == this->screen)
		hidden->gl.dirty = 1;

	DPRINTF("%p:pixels=%p, pitch=%p\n", surface, surface->pixels, surface->pitch);

	TRACE();

	return 0;

 unlock:
	eglUnlockSurfaceKHR(hidden->egl.dpy, hwdata->surface);
 out:
	ERROR();
	return -1;
}

void X11_UnlockHWSurface(SDL_VideoDevice *this, SDL_Surface *surface)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *hwdata = surface->hwdata;

	TRACE();

	DPRINTF("%p:pixels=%p, pitch=%p\n", surface, surface->pixels, surface->pitch);

	eglUnlockSurfaceKHR(hidden->egl.dpy, hwdata->surface);

	surface->pixels = NULL;
	surface->pitch = 0;

	TRACE();
}

int X11_FlipHWSurface(SDL_VideoDevice *this, SDL_Surface *surface)
{
	struct private_hwdata *hwdata = surface->hwdata;

	TRACE();

	DPRINTF("%p\n", surface);

	return 0;
}

int X11_SetHWColorKey(SDL_VideoDevice *this, SDL_Surface *surface, Uint32 key)
{
	struct private_hwdata *hwdata = surface->hwdata;

	TRACE();

	DPRINTF("%p:0x%08x\n", surface, key);

	return 0;
}

int X11_SetHWAlpha(SDL_VideoDevice *this, SDL_Surface *surface, Uint8 alpha)
{
	struct private_hwdata *hwdata = surface->hwdata;

	TRACE();

	DPRINTF("%p:0x%02x\n", surface, alpha);

	return 0;
}

static int X11_HWBlit(SDL_Surface *src, SDL_Rect *srcrect,
		      SDL_Surface *dst, SDL_Rect *dstrect)
{
	struct private_hwdata *srchwdata = src->hwdata;
	struct private_hwdata *dsthwdata = dst->hwdata;
	GLfloat x1 = 2.0f * dstrect->x / dst->w - 1.0f;
	GLfloat y1 = 2.0f * dstrect->y / dst->h - 1.0f;
	GLfloat x2 = 2.0f * (dstrect->x + dstrect->w) / dst->w - 1.0f;
	GLfloat y2 = 2.0f * (dstrect->y + dstrect->h) / dst->h - 1.0f;
	const GLfloat verts[] = {
		x1, y1,
		x1, y2,
		x2, y1,
		x2, y2,
	};
	x1 = (GLfloat) srcrect->x / src->w;
	y1 = (GLfloat) srcrect->y / src->h;
	x2 = (GLfloat) (srcrect->x + srcrect->w) / src->w;
	y2 = (GLfloat) (srcrect->y + srcrect->h) / src->h;
	const GLfloat texcoords[] = {
		x1, y1,
		x1, y2,
		x2, y1,
		x2, y2,
	};
	SDL_VideoDevice *this = dsthwdata->this;
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct rgba_color color;
	GLuint prog;

	TRACE();

	DPRINTF("%p:%dx%d+%d+%d -> %p:%dx%d+%d+%d\n",
		src, srcrect->w, srcrect->h, srcrect->x, srcrect->y,
		dst, dstrect->w, dstrect->h, dstrect->x, dstrect->y);

	if (glGetError())
		ERROR();

	bind_texture(hidden, srchwdata);
	bind_framebuffer(hidden, dsthwdata);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			srcrect->w > dstrect->w || srcrect->h > dstrect->h ?
			GL_LINEAR : GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			srcrect->w < dstrect->w || srcrect->h < dstrect->h ?
			GL_LINEAR : GL_NEAREST);

	glViewport(0, 0, dst->w, dst->h);

	if (src->flags & SDL_SRCCOLORKEY) {
		pixel_to_color(src->format, src->format->colorkey, &color);
		prog = hidden->gl.prog_ckey;
	} else
		prog = hidden->gl.prog_blit;

	color.a = 1.0f;

	if (src->flags & SDL_SRCALPHA) {
		/* either per-pixel alpha or global alpha, no modulation */
		if (!src->format->Amask)
			color.a = src->format->alpha / 255.0f;
		glEnable(GL_BLEND);
	}

	draw_quad(verts, texcoords, &color, prog);

	if (src->flags & SDL_SRCALPHA)
		glDisable(GL_BLEND);

	set_finish_hwdata(dsthwdata, dsthwdata);
	set_finish_hwdata(srchwdata, dsthwdata);

	if (dst == this->screen)
		hidden->gl.dirty = 1;

	if (glGetError())
		ERROR();

	TRACE();
}

int X11_CheckHWBlit(SDL_VideoDevice *this, SDL_Surface *src, SDL_Surface *dst)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *srchwdata = src->hwdata;
	struct private_hwdata *dsthwdata = dst->hwdata;

	TRACE();

	/* FIXME use a temp buffer? */
	if (src == dst)
		goto out;

	if (!srchwdata || !dsthwdata)
		goto out;

	/* Don't mix SW and HW access */
	if (src->locked || dst->locked)
		goto out;

	src->flags |= SDL_HWACCEL;
	src->map->hw_blit = X11_HWBlit;

	TRACE();

	return 0;

 out:
	ERROR();
	return -1;
}

int X11_CheckHWFill(SDL_VideoDevice *this, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *hwdata = dst->hwdata;

	TRACE();

	/* Don't mix SW and HW access */
	if (dst->locked)
		goto out;

	TRACE();

	return 1;

 out:
	ERROR();
	return 0;
}

int X11_FillHWRect(SDL_VideoDevice *this, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 pixel)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	struct private_hwdata *dsthwdata = dst->hwdata;
	GLfloat x1 = 2.0f * dstrect->x / dst->w - 1.0f;
	GLfloat y1 = 2.0f * dstrect->y / dst->h - 1.0f;
	GLfloat x2 = 2.0f * (dstrect->x + dstrect->w) / dst->w - 1.0f;
	GLfloat y2 = 2.0f * (dstrect->y + dstrect->h) / dst->h - 1.0f;
	const GLfloat verts[] = {
		x1, y1,
		x1, y2,
		x2, y1,
		x2, y2,
	};
	struct rgba_color color;

	TRACE();

	pixel_to_color(dst->format, pixel, &color);

	DPRINTF("0x%08x -> %p:%dx%d+%d+%d\n", color, dst, dstrect->w, dstrect->h, dstrect->x, dstrect->y);

	if (glGetError())
		ERROR();

	bind_framebuffer(hidden, dsthwdata);

	glViewport(0, 0, dst->w, dst->h);

	if (dstrect->w == dst->w && dstrect->h == dst->h) {
		DPRINTF("CLEAR %f %f %f %f\n", color.r, color.g, color.b, color.a);
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT);
	} else {
		draw_quad(verts, NULL, &color, hidden->gl.prog_fill);
	}

	set_finish_hwdata(dsthwdata, dsthwdata);

	if (dst == this->screen)
		hidden->gl.dirty = 1;

	if (glGetError())
		ERROR();

	TRACE();

	return 0;
}

void X11_RefreshDisplay(SDL_VideoDevice *this)
{
	SDL_Surface *screen = this->screen;

	TRACE();

	X11_ResizeImage(this, screen, screen->flags);

	/*
	 * Don't refresh a display that doesn't have an image (like GL)
	 * Instead, post an expose event so the application can refresh.
	 */
	if (!screen->hwdata) {
		SDL_PrivateExpose();
		return;
	}

	//FIXME?

	TRACE();
}

void X11_DisableAutoRefresh(SDL_VideoDevice *this)
{
	TRACE();
}

void X11_EnableAutoRefresh(SDL_VideoDevice *this)
{
	TRACE();
}

static inline void clamp(Sint16* val, int min, int size)
{
	if (*val < min || *val >= (min + size)) {
		*val = -1;
	} else {
		*val -= min;
	}
}

void X11_ScaleInput(SDL_VideoDevice *this, Sint16 *x, Sint16 *y)
{
	SDL_Surface *screen;

	if (!this || !this->screen)
		return;

	screen = this->screen;

	int dw = DisplayWidth(SDL_Display, SDL_Screen);
	int dh = DisplayHeight(SDL_Display, SDL_Screen);
	GLfloat srcaspect = (GLfloat) screen->w / screen->h;
	GLfloat dstaspect = (GLfloat) dw / dh;
	int _x, _y, _w, _h;

	if (srcaspect >= dstaspect) {
		_w = dw;
		_h = dw / srcaspect;
	} else {
		_w = dh * srcaspect;
		_h = dh;
	}
	_x = (dw - _w) / 2;
	_y = (dh - _h) / 2;

	clamp(x, _x, _w);
	clamp(y, _y, _h);

	if (*x >= 0)
		*x = *x * screen->w / _w;
	if (*y >= 0)
		*y = *y * screen->h / _h;
}

static void X11_UpdateRects(SDL_VideoDevice *this, int numrects, SDL_Rect *rects)
{
	struct SDL_PrivateVideoData *hidden = this->hidden;
	SDL_Surface *screen = this->screen;
	struct private_hwdata *hwdata = screen->hwdata;
	int dw = DisplayWidth(SDL_Display, SDL_Screen);
	int dh = DisplayHeight(SDL_Display, SDL_Screen);
	GLfloat srcaspect = (GLfloat) screen->w / screen->h;
	GLfloat dstaspect = (GLfloat) dw / dh;
	GLfloat w, h;

	TRACE();

	if (!hwdata)
		return;

	if (screen->locked)
		return; /* oops, what now? */

	if (!hidden->gl.dirty)
		return;

	/* Scale to fullscreen but keep pixels square */
	if (srcaspect >= dstaspect) {
		w = 1.0f;
		h = dstaspect / srcaspect;
	} else {
		w = srcaspect / dstaspect;
		h = 1.0f;
	}
	const GLfloat verts[] = {
		-w,  h,
		-w, -h,
		 w,  h,
		 w, -h,
	};
	const GLfloat texcoords[] = {
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 0.0f,
		1.0f, 1.0f,
	};
	const struct rgba_color color = { 1.0f, 1.0f, 1.0f, 1.0f };

	DPRINTF("%p:%dx%d+0+0 -> %dx%d+%d+%d\n", screen, screen->w, screen->h,
		(int) (w * dw), (int) (h * dh), (int) ((1.0f - w) * dw / 2.0f), (int) ((1.0f - h) * dh / 2.0f));

	if (glGetError())
		ERROR();

	bind_texture(hidden, hwdata);
	bind_framebuffer(hidden, NULL);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			screen->w > dw || screen->h > dh ?
			GL_LINEAR : GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			screen->w < dw || screen->h < dh ?
			GL_LINEAR : GL_NEAREST);

	glViewport(0, 0, dw, dh);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	draw_quad(verts, texcoords, &color, hidden->gl.prog_blit);

	eglSwapBuffers(hidden->egl.dpy, hidden->egl.surface);

	set_finish_hwdata(hwdata, &screen_hwdata);
	hidden->gl.dirty = 0;

	if (glGetError())
		ERROR();

	TRACE();
}
