
#include <fcntl.h>
#if defined(PANDORA) || defined(ODROID)
#define USE_FBIO 1
#endif

#ifdef USE_FBIO
#include <linux/fb.h>
#endif
#ifdef PANDORA
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "glx.h"
#include "utils.h"
//#include <GLES/gl.h>
#include "../gl/gl.h"
#include "../glx/streaming.h"

#define EXPORT __attribute__((visibility("default")))

#ifndef EGL_GL_COLORSPACE_KHR
#define EGL_GL_COLORSPACE_KHR                   0x309D
#define EGL_GL_COLORSPACE_SRGB_KHR              0x3089
#define EGL_GL_COLORSPACE_LINEAR_KHR            0x308A
#endif

static bool eglInitialized = false;
static EGLDisplay eglDisplay;
static EGLSurface eglSurface;
static EGLConfig eglConfigs[1];
static int glx_default_depth=0;
static int glx_surface_srgb=0;  // default to not try to create an sRGB surface
#ifdef PANDORA
static struct sockaddr_un sun;
static int sock = -2;
#endif

extern void* egl;


int8_t CheckEGLErrors() {
    EGLenum error;
    char *errortext;
    
    LOAD_EGL(eglGetError);

    error = egl_eglGetError();

    if (error != EGL_SUCCESS && error != 0) {
        switch (error) {
            case EGL_NOT_INITIALIZED:     errortext = "EGL_NOT_INITIALIZED"; break;
            case EGL_BAD_ACCESS:          errortext = "EGL_BAD_ACCESS"; break;
            case EGL_BAD_ALLOC:           errortext = "EGL_BAD_ALLOC"; break;
            case EGL_BAD_ATTRIBUTE:       errortext = "EGL_BAD_ATTRIBUTE"; break;
            case EGL_BAD_CONTEXT:         errortext = "EGL_BAD_CONTEXT"; break;
            case EGL_BAD_CONFIG:          errortext = "EGL_BAD_CONFIG"; break;
            case EGL_BAD_CURRENT_SURFACE: errortext = "EGL_BAD_CURRENT_SURFACE"; break;
            case EGL_BAD_DISPLAY:         errortext = "EGL_BAD_DISPLAY"; break;
            case EGL_BAD_SURFACE:         errortext = "EGL_BAD_SURFACE"; break;
            case EGL_BAD_MATCH:           errortext = "EGL_BAD_MATCH"; break;
            case EGL_BAD_PARAMETER:       errortext = "EGL_BAD_PARAMETER"; break;
            case EGL_BAD_NATIVE_PIXMAP:   errortext = "EGL_BAD_NATIVE_PIXMAP"; break;
            case EGL_BAD_NATIVE_WINDOW:   errortext = "EGL_BAD_NATIVE_WINDOW"; break;
            default:                      errortext = "unknown"; break;
        }

        printf("ERROR: EGL Error detected: %s (0x%X)\n", errortext, error);
        return 1;
    }

    return 0;
}

static int get_config_default(int attribute, int *value) {
    switch (attribute) {
        case GLX_USE_GL:
        case GLX_RGBA:
        case GLX_DOUBLEBUFFER:
            *value = 1;
            break;
        case GLX_LEVEL:
        case GLX_STEREO:
            *value = 0;
            break;
        case GLX_AUX_BUFFERS:
            *value = 0;
            break;
        case GLX_RED_SIZE:
            *value = 5;
            break;
        case GLX_GREEN_SIZE:
            *value = 6;
            break;
        case GLX_BLUE_SIZE:
            *value = 5;
            break;
        case GLX_ALPHA_SIZE:
            *value = 8;
            break;
        case GLX_DEPTH_SIZE:
            *value = 16;
            break;
        case GLX_STENCIL_SIZE:
        case GLX_ACCUM_RED_SIZE:
        case GLX_ACCUM_GREEN_SIZE:
        case GLX_ACCUM_BLUE_SIZE:
        case GLX_ACCUM_ALPHA_SIZE:
            *value = 0;
            break;
        case GLX_TRANSPARENT_TYPE:
            *value = GLX_NONE;
            break;
        case GLX_RENDER_TYPE:
            *value = GLX_RGBA_BIT | GLX_COLOR_INDEX_BIT;
            break;
        case GLX_VISUAL_ID:
            *value = 1;
            break;
        case GLX_FBCONFIG_ID:
            *value = 1;
            break;
        case GLX_DRAWABLE_TYPE:
            *value = GLX_WINDOW_BIT;
            break;
        case GLX_BUFFER_SIZE:
             *value = 16;
            break;
        case GLX_X_VISUAL_TYPE:
        case GLX_CONFIG_CAVEAT:
        case GLX_SAMPLE_BUFFERS:
        case GLX_SAMPLES:
            *value = 0;
            break;
        case GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB:
            *value = 0;
            break;
        default:
            printf("libGL: unknown attrib %i\n", attribute);
            *value = 0;
            return 1;
    }
    return 0;
}



// hmm...
static EGLContext eglContext;

#ifndef ANDROID
static GLXContext glxContext = NULL;
static GLXContext fbContext = NULL;
#endif //ANDROID

static int fbcontext_count = 0;

#ifdef USE_FBIO
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
#ifdef PANDORA
static float pandora_gamma = 0.0f;
#endif
static int fbdev = -1;
static bool g_vsync = false;
#endif
static bool g_showfps = false;
static bool g_usefb = true;
static bool g_usefbo = false;
static bool g_xrefresh = false;
static bool g_stacktrace = false;
extern int automipmap;
extern int texcopydata;
extern int tested_env;
extern int texshrink;
extern int texdump;
extern int alphahack;
extern int texstream;
extern int copytex;
extern int nolumalpha;
extern int blendhack;
extern int export_blendcolor;
extern int glshim_noerror;
extern char glshim_version[50];
extern int glshim_nobanner;
extern int glshim_npot;
int export_silentstub = 0;
int glshim_queries = 0;

bool g_recyclefbo = false;
static int  g_width=0, g_height=0;
// RPI stuffs
static bool g_bcmhost = false;
static bool g_bcm_active = false;
void (*bcm_host_init)();
void (*bcm_host_deinit)();

#define SHUT(a) if(!glshim_nobanner) a

static EGLDisplay get_egl_display(Display *display) {
    static EGLDisplay eglDisplay = NULL;
    LOAD_EGL(eglGetDisplay);
	eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    return eglDisplay;
}

static int swap_interval = 1;
#ifndef ANDROID
static void init_display(Display *display) {
    LOAD_EGL(eglGetDisplay);
    
    eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);

}
#endif //ANDROID
static void init_vsync() {
#ifdef USE_FBIO
    fbdev = open("/dev/fb0", O_RDONLY);
    if (fbdev < 0) {
        fprintf(stderr, "Could not open /dev/fb0 for vsync.\n");
    }
#endif
}

static void xrefresh() {
    int dummy = system("xrefresh");
}

#ifdef PANDORA
static void pandora_reset_gamma() {
    if(pandora_gamma>0.0f)
        system("sudo /usr/pandora/scripts/op_gamma.sh 0");
}
static void pandora_set_gamma() {
    if(pandora_gamma>0.0f) {
        char buf[50];
        sprintf(buf, "sudo /usr/pandora/scripts/op_gamma.sh %.2f", pandora_gamma);
        int dummy = system(buf);
    }
}
#endif

static void signal_handler(int sig) {
    if (g_xrefresh)
        xrefresh();
#ifdef PANDORA
    pandora_reset_gamma();
#endif

#ifdef BCMHOST
    if (g_bcm_active) {
        g_bcm_active = false;
        bcm_host_deinit();
    }
#endif

    signal(sig, SIG_DFL);
    raise(sig);
}
#ifdef PANDORA
static void init_liveinfo() {
    static const char socket_name[] = "\0liveinfo";
    sock = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1) {
        // no socket, so LiveInfo probably not active
        return;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    memcpy(sun.sun_path, socket_name, sizeof(socket_name));
    // send a test string
    const char test_string[] = "gl: fpsinfo";
    if (sendto(sock, test_string, strlen(test_string), 0,(struct sockaddr *)&sun, sizeof(sun))<0) {
        // error, so probably not present
        close(sock);
        sock=-1;
    } else
        fcntl(sock, F_SETFL, O_NONBLOCK);
}

static void fast_math() {
  // enable Cortex A8 RunFast
   int v = 0;
   __asm__ __volatile__ (
     "vmrs %0, fpscr\n"
     "orr  %0, #((1<<25)|(1<<24))\n" // default NaN, flush-to-zero
     "vmsr fpscr, %0\n"
     //"vmrs %0, fpscr\n"
     : "=&r"(v));
}
#endif
extern void initialize_glshim();
extern int initialized;
static void scan_env() {
    static bool first = true;
    if (! first)
        return;
    if (! initialized)
    {
	initialize_glshim();
    }
    /* Check for some corruption inside state.... */
    if ((glstate.texture.active < 0) || (glstate.texture.active > MAX_TEX) || 
        (glstate.vao->pointers.vertex.buffer!= 0) || (glstate.vao->vertex != 0) || (glstate.list.active!=0)) {
        SHUT(printf("LIBGL: Warning, memory corruption detected at init, trying to compensate\n"));
        initialize_glshim();
    }
    first = false;
    SHUT(printf("libGL: built on %s %s\n", __DATE__, __TIME__));
    #define env(name, global, message)                    \
        char *env_##name = getenv(#name);                 \
        if (env_##name && strcmp(env_##name, "1") == 0) { \
            SHUT(printf("libGL: " message "\n"));         \
            global = true;                                \
        }

    env(LIBGL_XREFRESH, g_xrefresh, "xrefresh will be called on cleanup");
    env(LIBGL_STACKTRACE, g_stacktrace, "stacktrace will be printed on crash");
    // if ok, grab the init/deinit functions
    if (bcm_host) {
        bcm_host_init = dlsym(bcm_host, "bcm_host_init");
        bcm_host_deinit = dlsym(bcm_host, "bcm_host_deinit");
        if (bcm_host_init && bcm_host_deinit)
            g_bcmhost = true;
    }
    if (g_xrefresh || g_stacktrace || g_bcmhost) {
        // TODO: a bit gross. Maybe look at this: http://stackoverflow.com/a/13290134/293352
        signal(SIGBUS, signal_handler);
        signal(SIGFPE, signal_handler);
        signal(SIGILL, signal_handler);
        signal(SIGSEGV, signal_handler);
        if (g_xrefresh || g_bcmhost) {
            signal(SIGINT, signal_handler);
            signal(SIGQUIT, signal_handler);
            signal(SIGTERM, signal_handler);
        }
        if (g_xrefresh)
            atexit(xrefresh);
#ifndef ANDROID	
#ifdef BCMHOST
            atexit(bcm_host_deinit);
#endif
#endif //ANDROID
    }
    env(LIBGL_FB, g_usefb, "framebuffer output enabled");
    if (env_LIBGL_FB && strcmp(env_LIBGL_FB, "2") == 0) {
            SHUT(printf("libGL: using framebuffer + fbo\n"));
            g_usefb = true;
            g_usefbo = true;
    }
    env(LIBGL_FPS, g_showfps, "fps counter enabled");
#ifdef USE_FBIO
    env(LIBGL_VSYNC, g_vsync, "vsync enabled");
    if (g_vsync) {
        init_vsync();
    }
#endif
#ifdef PANDORA
    init_liveinfo();
    if (sock>-1) {
        SHUT(printf("LIBGL: LiveInfo detected, fps will be shown\n"));
    }
#endif
    env(LIBGL_RECYCLEFBO, g_recyclefbo, "Recycling of FBO enabled");
    // Texture hacks
    char *env_mipmap = getenv("LIBGL_MIPMAP");
    if (env_mipmap && strcmp(env_mipmap, "1") == 0) {
        automipmap = 1;
        SHUT(printf("LIBGL: AutoMipMap forced\n"));
    }
    if (env_mipmap && strcmp(env_mipmap, "2") == 0) {
        automipmap = 2;
        SHUT(printf("LIBGL: guess AutoMipMap\n"));
    }
    if (env_mipmap && strcmp(env_mipmap, "3") == 0) {
        automipmap = 3;
        printf("LIBGL: ignore MipMap\n");
    }
    if (env_mipmap && strcmp(env_mipmap, "4") == 0) {
        automipmap = 4;
        SHUT(printf("LIBGL: ignore AutoMipMap on non-squared textures\n"));
    }
    char *env_texcopy = getenv("LIBGL_TEXCOPY");
    if (env_texcopy && strcmp(env_texcopy, "1") == 0) {
        texcopydata = 1;
        SHUT(printf("LIBGL: Texture copy enabled\n"));
    }
    char *env_shrink = getenv("LIBGL_SHRINK");
    if (env_shrink && strcmp(env_shrink, "1") == 0) {
        texshrink = 1;
        SHUT(printf("LIBGL: Texture shink, mode 1 selected (everything / 2)\n"));
    }
    if (env_shrink && strcmp(env_shrink, "2") == 0) {
        texshrink = 2;
        printf("LIBGL: Texture shink, mode 2 selected (only > 512 /2 )\n");
    }
    if (env_shrink && strcmp(env_shrink, "3") == 0) {
        texshrink = 3;
        SHUT(printf("LIBGL: Texture shink, mode 3 selected (only > 256 /2 )\n"));
    }
    if (env_shrink && strcmp(env_shrink, "4") == 0) {
        texshrink = 4;
        SHUT(printf("LIBGL: Texture shink, mode 4 selected (only > 256 /2, >=1024 /4 )\n"));
    }
    if (env_shrink && strcmp(env_shrink, "5") == 0) {
        texshrink = 5;
        SHUT(printf("LIBGL: Texture shink, mode 5 selected (every > 256 is downscaled to 256 ), but not for empty texture\n"));
    }
    if (env_shrink && strcmp(env_shrink, "6") == 0) {
        texshrink = 6;
        SHUT(printf("LIBGL: Texture shink, mode 6 selected (only > 128 /2, >=512 is downscaled to 256 ), but not for empty texture\n"));
    }
    if (env_shrink && strcmp(env_shrink, "7") == 0) {
        texshrink = 7;
        SHUT(printf("LIBGL: Texture shink, mode 7 selected (only > 512 /2 ), but not for empty texture\n"));
    }
    if (env_shrink && strcmp(env_shrink, "8") == 0) {
        texshrink = 8;
        SHUT(printf("LIBGL: Texture shink, mode 8 selected (advertise 8192 max texture size, but >2048 are shrinked to 2048)\n"));
    }
    if (env_shrink && strcmp(env_shrink, "9") == 0) {
        texshrink = 9;
        SHUT(printf("LIBGL: Texture shink, mode 9 selected (advertise 8192 max texture size, but >4096 are quadshrinked and > 512 are shrinked), but not for empty texture\n"));
    }
    if (env_shrink && strcmp(env_shrink, "10") == 0) {
        texshrink = 10;
        SHUT(printf("LIBGL: Texture shink, mode 10 selected (advertise 8192 max texture size, but >2048 are quadshrinked and > 512 are shrinked), but not for empty texture\n"));
    }
    char *env_dump = getenv("LIBGL_TEXDUMP");
    if (env_dump && strcmp(env_dump, "1") == 0) {
        texdump = 1;
        SHUT(printf("LIBGL: Texture dump enabled\n"));
    }
    char *env_alpha = getenv("LIBGL_ALPHAHACK");
    if (env_alpha && strcmp(env_alpha, "1") == 0) {
        alphahack = 1;
        SHUT(printf("LIBGL: Alpha Hack enabled\n"));
    }
#ifdef TEXSTREAM
    char *env_stream = getenv("LIBGL_STREAM");
    if (env_stream && strcmp(env_stream, "1") == 0) {
        texstream = InitStreamingCache();
        SHUT(printf("LIBGL: Streaming texture %s\n",(texstream)?"enabled":"not available"));
        //FreeStreamed(AddStreamed(1024, 512, 0));
    }
    if (env_stream && strcmp(env_stream, "2") == 0) {
        texstream = InitStreamingCache()?2:0;
        SHUT(printf("LIBGL: Streaming texture %s\n",(texstream)?"forced":"not available"));
        //FreeStreamed(AddStreamed(1024, 512, 0));
    }
#endif
    char *env_copy = getenv("LIBGL_COPY");
    if (env_copy && strcmp(env_copy, "1") == 0) {
        SHUT(printf("LIBGL: No glCopyTexImage2D / glCopyTexSubImage2D hack\n"));
        copytex = 1;
    }
    char *env_lumalpha = getenv("LIBGL_NOLUMALPHA");
    if (env_lumalpha && strcmp(env_lumalpha, "1") == 0) {
        nolumalpha = 1;
        SHUT(printf("LIBGL: GL_LUMINANCE_ALPHA hardware support disabled\n"));
    }

    env(LIBGL_BLENDHACK, blendhack, "Change Blend GL_SRC_ALPHA, GL_ONE to GL_ONE, GL_ONE");
    env(LIBGL_BLENDCOLOR, export_blendcolor, "Export a (faked) glBlendColor");
    env(LIBGL_NOERROR, glshim_noerror, "glGetError() always return GL_NOERROR");
    env(LIBGL_SILENTSTUB, export_silentstub, "Stub/non present functions are not printed");
    
    char *env_version = getenv("LIBGL_VERSION");
    if (env_version) {
        SHUT(printf("LIBGL: Overide version string with \"%s\" (should be in the form of \"1.x\")\n", env_version));
    }
    snprintf(glshim_version, 49, "%s glshim wrapper", (env_version)?env_version:"1.5");
#ifdef PANDORA
    char *env_gamma = getenv("LIBGL_GAMMA");
    if (env_gamma) {
        pandora_gamma=atof(env_gamma);
        SHUT(printf("LIBGL: Set gamma to %.2f\n", pandora_gamma));
        atexit(pandora_reset_gamma);
    }
#endif
    char *env_srgb = getenv("LIBGL_SRGB");
    if (env_srgb && strcmp(env_srgb, "1") == 0) {
        glx_surface_srgb = 1;
        SHUT(printf("LIBGL: enabling sRGB support\n"));
    }
    char *env_fastmath = getenv("LIBGL_FASTMATH");
    if (env_fastmath && strcmp(env_fastmath, "1") == 0) {
#ifdef PANDORA
        SHUT(printf("LIBGL: Enable FastMath for cortex-a8\n"));
        fast_math();
#else
        printf("LIBGL: No FastMath on this platform\n");
#endif
    }
    char *env_npot = getenv("LIBGL_NPOT");
    if (env_npot && strcmp(env_npot, "1") == 0) {
		glshim_npot = 1;
		SHUT(printf("Expose limited NPOT extension\n"));
	}
    if (env_npot && strcmp(env_npot, "2") == 0) {
		glshim_npot = 2;
		SHUT(printf("Expose GL_ARB_texture_non_power_of_two extension\n"));
	}
   char *env_queries = getenv("LIBGL_GLQUERIES");
    if (env_queries && strcmp(env_queries, "1") == 0) {
        glshim_queries = 1;
        SHUT(printf("Expose fake glQueries functions\n"));
    }
     
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))!= NULL)
        SHUT(printf("LIBGL: Current folder is:%s\n", cwd));
}
#ifndef ANDROID	
EXPORT GLXContext glXCreateContext(Display *display,
                            XVisualInfo *visual,
                            GLXContext shareList,
                            Bool isDirect) {
//printf("glXCreateContext(%p, %p, %p, %i)\n", display, visual, shareList, isDirect);
    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_NONE
    };

    EGLint attrib_list[] = {
        EGL_NONE
    };

    scan_env();
    
    if (g_usefb && fbcontext_count>0) {
        // don't create a new context, one FB is enough...
        fbcontext_count++;
        return fbContext;
    }

#ifdef BCMHOST
    if (! g_bcm_active) {
        g_bcm_active = true;
        bcm_host_init();
    }
#endif

    LOAD_EGL(eglMakeCurrent);
    LOAD_EGL(eglDestroyContext);
    LOAD_EGL(eglDestroySurface);
    LOAD_EGL(eglBindAPI);
    LOAD_EGL(eglInitialize);
    LOAD_EGL(eglCreateContext);
    LOAD_EGL(eglChooseConfig);
    LOAD_EGL(eglQueryString);
    
    GLXContext fake = malloc(sizeof(struct __GLXContextRec));
	memset(fake, 0, sizeof(struct __GLXContextRec));
	if (!g_usefb) {
		if (eglDisplay != NULL)
			egl_eglMakeCurrent(eglDisplay, NULL, NULL, EGL_NO_CONTEXT);	// just in case some context is already attached. *TODO: track that?
	} else {
		if (eglDisplay != NULL) {
            if (eglSurface!=NULL) {
                // A surface already exist....
                    fake->display = 0;
                    fake->direct = true;
                    fake->xid = 1;  //TODO: Proper handling of that id...
                    return fake; // Do nothing
            }
			egl_eglMakeCurrent(eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
			if (eglContext != NULL) {
				egl_eglDestroyContext(eglDisplay, eglContext);
				eglContext = NULL;
			}
			if (eglSurface != NULL) {
				egl_eglDestroySurface(eglDisplay, eglSurface);
				eglSurface = NULL;
			}
		}
        fbContext = fake;
	}
    // make an egl context here...
    EGLBoolean result;
    if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
        init_display(display);
        if (eglDisplay == EGL_NO_DISPLAY) {
            printf("Unable to create EGL display.\n");
            return fake;
        }
    }

    // first time?
    if (eglInitialized == false) {
        egl_eglBindAPI(EGL_OPENGL_ES_API);
        result = egl_eglInitialize(eglDisplay, NULL, NULL);
        if (result != EGL_TRUE) {
            printf("Unable to initialize EGL display.\n");
            return fake;
        }
        eglInitialized = true;
    }

    // With the display, try to check if sRGB surface are supported
    if (glx_surface_srgb==1) {
        if(strstr(egl_eglQueryString(eglDisplay, EGL_EXTENSIONS), "EGL_KHR_gl_colorspace")) {
            printf("LIBGL: sRGB surface supported\n");
            glx_surface_srgb=2; // test only 1 time
        } else
            glx_surface_srgb=0;
    }

    int configsFound;
	if (!g_usefb)
		result = egl_eglChooseConfig(eglDisplay, configAttribs, fake->eglConfigs, 1, &configsFound);
	else
		result = egl_eglChooseConfig(eglDisplay, configAttribs, eglConfigs, 1, &configsFound);

    CheckEGLErrors();
    if (result != EGL_TRUE || configsFound == 0) {
        printf("No EGL configs found.\n");
        return fake;
    }
    EGLContext shared = (shareList)?shareList->eglContext:EGL_NO_CONTEXT;
	if (!g_usefb)
		fake->eglContext = egl_eglCreateContext(eglDisplay, fake->eglConfigs[0], shared, attrib_list);
	else {
		eglContext = egl_eglCreateContext(eglDisplay, eglConfigs[0], shared, attrib_list);
        fake->eglContext = eglContext;
    }

    CheckEGLErrors();

    // need to return a glx context pointing at it
    fake->display = 0;
    fake->direct = true;
    fake->xid = 1;  //TODO: Proper handling of that id...
   	if (!g_usefb) {
		// unassign the context, it's not supposed to be active at the creation
		egl_eglMakeCurrent(eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
	}

	// *TODO* put eglContext inside GLXcontext, to handle multiple Glxcontext
        
    return fake;
}

EXPORT GLXContext glXCreateContextAttribsARB(Display *display, void *config,
                                      GLXContext share_context, Bool direct,
                                      const int *attrib_list) {
    return glXCreateContext(display, NULL, NULL, direct);
}

EXPORT void glXDestroyContext(Display *display, GLXContext ctx) {
//printf("glXDestroyContext(%p, %p)\n", display, ctx);
    if (g_usefb) {
        if (fbcontext_count==0)
            return; // Should not happens!
        if (--fbcontext_count > 0)
            return; // Nothing to do...
    }
    if ((!g_usefb && ctx->eglContext) || (g_usefb && eglContext)) {
        if (g_usefbo) {
            deleteMainFBO();
        }
        
        LOAD_EGL(eglDestroyContext);
        LOAD_EGL(eglDestroySurface);
        
		EGLBoolean result = egl_eglDestroyContext(eglDisplay, (g_usefb)?eglContext:ctx->eglContext);
        eglContext = NULL;
        if (!g_usefb && ctx->eglSurface != NULL) {
            egl_eglDestroySurface(eglDisplay, ctx->eglSurface);
			eglSurface = ctx->eglSurface = NULL;
        }
        if (g_usefb && eglSurface != NULL) {
            egl_eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = NULL;
        }
        if (result != EGL_TRUE) {
            printf("Failed to destroy EGL context.\n");
        }
        /*if (fbdev >= 0) {
            close(fbdev);
            fbdev = -1;
        }*/
    }
    if (g_usefb)
        fbContext = NULL;
        
    return;
}

EXPORT Display *glXGetCurrentDisplay() {
    return 0;
}

EXPORT XVisualInfo *glXChooseVisual(Display *display,
                             int screen,
                             int *attributes) {

	static XVisualInfo fake = {0};
	return &fake;
}

/*
EGL_BAD_MATCH is generated if draw or read are not compatible with context
or if context is set to EGL_NO_CONTEXT and draw or read are not set to
EGL_NO_SURFACE, or if draw or read are set to EGL_NO_SURFACE and context is
not set to EGL_NO_CONTEXT.
*/

EXPORT Bool glXMakeCurrent(Display *display,
                    GLXDrawable drawable,
                    GLXContext context) {
	Display *dpy;					
    LOAD_EGL(eglCreateWindowSurface);
    LOAD_EGL(eglDestroySurface);
    LOAD_EGL(eglMakeCurrent);
    EGLDisplay eglDisplay = get_egl_display(dpy);
    if (eglDisplay != NULL) {
        egl_eglMakeCurrent(eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
        if (eglSurface != NULL) {
            egl_eglDestroySurface(eglDisplay, eglSurface);
        }
    }
    // call with NULL to just destroy old stuff.
    if (! context) {
        return true;
    }
    
	static EGLint const window_attribute_list[] = {
		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
		EGL_NONE
	};
    
    if (g_usefb)
        drawable = 0;
    eglSurface = egl_eglCreateWindowSurface(eglDisplay, eglConfigs[0], drawable, window_attribute_list);
    CheckEGLErrors();

    EGLBoolean result = egl_eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    CheckEGLErrors();
    if (result) {
        return true;
    }
    return false;
}

EXPORT Bool glXMakeContextCurrent(Display *display, int drawable,
                           int readable, GLXContext context) {
    return glXMakeCurrent(display, drawable, context);
}

EXPORT void glXSwapBuffers(Display *display,
                    int drawable) {
    static int frames = 0;
    
    LOAD_EGL(eglSwapBuffers);
    int old_batch = glstate.gl_batch;
    if (glstate.gl_batch || glstate.list.active){
        flush();
    }
#ifdef USE_FBIO
    if (g_vsync && fbdev >= 0) {
        // TODO: can I just return if I don't meet vsync over multiple frames?
        // this will just block otherwise.
        int arg = 0;
        for (int i = 0; i < swap_interval; i++) {
            ioctl(fbdev, FBIO_WAITFORVSYNC, &arg);
        }
    }
#endif
    if (g_usefbo) {
        glstate.gl_batch = 0;
        unbindMainFBO();
        blitMainFBO();
        // blit the main_fbo before swap
    }

    egl_eglSwapBuffers(eglDisplay, eglSurface);
    CheckEGLErrors();
#ifdef PANDORA
    if (g_showfps || (sock>-1)) {
        // framerate counter
        static float avg, fps = 0;
        static int frame1, last_frame, frame, now, current_frames;
        struct timeval out;
        gettimeofday(&out, NULL);
        now = out.tv_sec;
        frame++;
        current_frames++;

        if (frame == 1) {
            frame1 = now;
        } else if (frame1 < now) {
            if (last_frame < now) {
                float change = current_frames / (float)(now - last_frame);
                float weight = 0.7;
                if (! fps) {
                    fps = change;
                } else {
                    fps = (1 - weight) * fps + weight * change;
                }
                current_frames = 0;

                avg = frame / (float)(now - frame1);
                if (g_showfps) printf("libGL fps: %.2f, avg: %.2f\n", fps, avg);
                if (sock>-1) {
                    char tmp[60];
                    snprintf(tmp, 60, "gl:  %2.2f", fps);
                    sendto(sock, tmp, strlen(tmp), 0,(struct sockaddr *)&sun, sizeof(sun));                    
                }
            }
        }
        last_frame = now;
    }
#endif
    if (g_usefbo) {
        glstate.gl_batch = old_batch;
        bindMainFBO();
    }
}

EXPORT int glXGetConfig(Display *display,
                 XVisualInfo *visual,
                 int attribute,
                 int *value) {
    return get_config_default(attribute, value);
}

EXPORT const char *glXQueryExtensionsString(Display *display, int screen) {
    const char *extensions = {
        "GLX_ARB_create_context "
        "GLX_ARB_create_context_profile "
        "GLX_EXT_create_context_es2_profile "
        "GLX_ARB_get_proc_address "
    };
    return extensions;
}

EXPORT const char *glXQueryServerString(Display *display, int screen, int name) {
    return "";
}

EXPORT Bool glXQueryExtension(Display *display, int *errorBase, int *eventBase) {
    if (errorBase)
        *errorBase = 0;

    if (eventBase)
        *eventBase = 0;

    return true;
}

EXPORT Bool glXQueryVersion(Display *display, int *major, int *minor) {
    // TODO: figure out which version we want to pretend to implement
    *major = 1;
    *minor = 4;
    return true;
}

EXPORT const char *glXGetClientString(Display *display, int name) {
    // TODO: return actual data here
    switch (name) {
        case GLX_VENDOR: return "ptitSeb";
        case GLX_VERSION: return "1.4 OpenPandora";
        case GLX_EXTENSIONS: break;
    }
    return "";
}

EXPORT int glXQueryContext( Display *dpy, GLXContext ctx, int attribute, int *value ){
	*value=0;
	if (ctx) switch (attribute) {
		case GLX_FBCONFIG_ID: *value=ctx->xid; break;
		case GLX_RENDER_TYPE: *value=GLX_RGBA_TYPE; break;
		case GLX_SCREEN: break;			// screen n# is always 0
	}
    return 0;
}

/*
EXPORT void glXQueryDrawable( Display *dpy, int draw, int attribute,
                       unsigned int *value ) {
	*value=0;
}
*/
// stubs for glfw (GLX 1.3)
EXPORT GLXContext glXGetCurrentContext() {
    // hack to make some games start
    if (g_usefb)
		return glxContext ? glxContext : fbContext;
	else
		return glxContext;
}

EXPORT GLXFBConfig *glXChooseFBConfig(Display *display, int screen,
                       const int *attrib_list, int *count) {
    *count = 1;
    GLXFBConfig *configs = malloc(sizeof(GLXFBConfig) * *count);
    return configs;
}
EXPORT GLXFBConfig *glXChooseFBConfigSGIX(Display *display, int screen,
                       const int *attrib_list, int *count) {
    return glXChooseFBConfig(display, screen, attrib_list, count);
}

EXPORT GLXFBConfig *glXGetFBConfigs(Display *display, int screen, int *count) {
    *count = 1;
    GLXFBConfig *configs = malloc(sizeof(GLXFBConfig) * *count);
    return configs;
}

EXPORT int glXGetFBConfigAttrib(Display *display, GLXFBConfig config, int attribute, int *value) {
    return get_config_default(attribute, value);
}

EXPORT XVisualInfo *glXGetVisualFromFBConfig(Display *display, GLXFBConfig config) {
	static XVisualInfo fake = {0};
	return &fake;
}

EXPORT GLXContext glXCreateNewContext(Display *display, GLXFBConfig config,
                               int render_type, GLXContext share_list,
                               Bool is_direct) {
printf("glXCreateNewContext(%p, %p, %d, %p, %i)\n", display, config, render_type, share_list, is_direct);
    return glXCreateContext(display, 0, share_list, is_direct);
}
#endif //ANDROID
EXPORT void glXSwapIntervalMESA(int interval) {
    printf("glXSwapInterval(%i)\n", interval);
#ifdef USE_FBIO
    if (! g_vsync)
        printf("Enable LIBGL_VSYNC=1 if you want to use vsync.\n");
    swap_interval = interval;
#else
    LOAD_EGL(eglSwapInterval);
    egl_eglSwapInterval(eglDisplay, swap_interval);
#endif
}

EXPORT void glXSwapIntervalSGI(int interval) {
    glXSwapIntervalMESA(interval);
}

#ifndef ANDROID
EXPORT void glXSwapIntervalEXT(Display *display, int drawable, int interval) {
    glXSwapIntervalMESA(interval);
}

// misc stubs
EXPORT void glXCopyContext(Display *display, GLXContext src, GLXContext dst, GLuint mask) {
	// mask is ignored for now, but should include glPushAttrib / glPopAttrib
	memcpy(dst, src, sizeof(struct __GLXContextRec));
}
EXPORT void glXCreateGLXPixmap(Display *display, XVisualInfo * visual, Pixmap pixmap) {} // should return GLXPixmap
EXPORT void glXDestroyGLXPixmap(Display *display, void *pixmap) {} // really wants a GLXpixmap
EXPORT Window glXCreateWindow(Display *display, GLXFBConfig config, Window win, int *attrib_list) {return win;} // should return GLXWindow
EXPORT void glXDestroyWindow(Display *display, void *win) {} // really wants a GLXWindow

EXPORT GLXDrawable glXGetCurrentDrawable() {
	if (glxContext) 
		return glxContext->drawable; 
	else 
		return 0;
} // this should actually return GLXDrawable.

EXPORT Bool glXIsDirect(Display * display, GLXContext ctx) {
    return true;
}

EXPORT void glXUseXFont(Font font, int first, int count, int listBase) {

}
#endif //ANDROID
EXPORT void glXWaitGL() {}
EXPORT void glXWaitX() {}
EXPORT void glXReleaseBuffersMESA() {}
#ifndef ANDROID
/* TODO proper implementation */
EXPORT int glXQueryDrawable(Display *dpy, GLXDrawable draw, int attribute,	unsigned int *value) {
    *value = 0;
    switch(attribute) {
        case GLX_WIDTH:
            *value = 320;
            return 1;
        case GLX_HEIGHT:
            *value = 240;
            return 1;
        case GLX_PRESERVED_CONTENTS:
            return 0;
        case GLX_LARGEST_PBUFFER:
            return 0;
        case GLX_FBCONFIG_ID:
            *value = 1;
            return 1;
    }
    return 0;
}

EXPORT void glXDestroyPbuffer(Display * dpy, GLXPbuffer pbuf) {
    printf("LIBGL: Warning, stub glxDestroyPBuffer called\n");
}
EXPORT GLXPbuffer glXCreatePbuffer(Display * dpy, GLXFBConfig config, const int * attrib_list) {
    printf("LIBGL: Warning, stub glxCreatePBuffer called\n");
    return 0;
}
#endif //ANDROID
