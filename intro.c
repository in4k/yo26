#ifndef XRES
#define XRES 1280
#endif
#ifndef YRES
#define YRES 720
#endif
//#define DO_RANGES

#define SOUND

#ifdef _DEBUG
#ifdef FULLSCREEN
#undef FULLSCREEN
#endif
#define DEBUG_FUNCLOAD
#define SHADER_DEBUG
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#define VC_LEANMEAN
#define VC_EXTRALEAN

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <GL/gl.h>

#if defined(CAPTURE)
#include <stdio.h>
#ifdef __linux__
#include <unistd.h>
#endif
#endif

#ifdef _DEBUG
//#pragma data_seg(".fltused")
int _fltused = 1;
#endif

#define oglCreateShaderProgramv gl.CreateShaderProgramv
#define oglGenFramebuffers gl.GenFramebuffers
#define oglBindFramebuffer gl.BindFramebuffer
#define oglFramebufferTexture2D gl.FramebufferTexture2D
#define oglUseProgram gl.UseProgram
#define oglGetUniformLocation gl.GetUniformLocation
#define oglUniform1iv gl.Uniform1iv
#define oglUniform1fv gl.Uniform1fv
#define oglDrawBuffers gl.DrawBuffers
#define oglActiveTexture gl.ActiveTexture

#elif defined(__linux__)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#define __stdcall
#define __forceinline inline __attribute__((always_inline))

#define oglCreateShaderProgramv glCreateShaderProgramv
#define oglGenFramebuffers glGenFramebuffers
#define oglBindFramebuffer glBindFramebuffer
#define oglFramebufferTexture2D glFramebufferTexture2D
#define oglUseProgram glUseProgram
#define oglGetUniformLocation glGetUniformLocation
#define oglUniform1i glUniform1i
#define oglUniform3f glUniform3f
#endif

#include "glext.h"

#ifdef OLD_4KLANG
//#include "music/4klang.h"
#define SAMPLE_RATE 44100
#define BPM 92.000000
#define MAX_INSTRUMENTS 8
#define MAX_PATTERNS 49
#define PATTERN_SIZE_SHIFT 4
#define PATTERN_SIZE (1 << PATTERN_SIZE_SHIFT)
#define MAX_TICKS (MAX_PATTERNS*PATTERN_SIZE)
#define SAMPLES_PER_TICK 7190
#define MAX_SAMPLES (SAMPLES_PER_TICK*MAX_TICKS)
#define POLYPHONY 2
#define FLOAT_32BIT
#define SAMPLE_TYPE float
#define INTRO_LENGTH (1000ull * MAX_SAMPLES / SAMPLE_RATE)
#endif

#define SOUND_SAMPLERATE 44100
#define BPM 160
#define BAR_TICKS 32
#define SAMPLES_PER_BAR (SOUND_SAMPLERATE * 4 * 60 / BPM)
#define SAMPLES_PER_TICK (SAMPLES_PER_BAR / BAR_TICKS)
#define LENGTH_BARS 32

#define SOUND_SAMPLES (SAMPLES_PER_BAR * LENGTH_BARS)
#define SAMPLE_TYPE float
#define FLOAT_32BIT

#define BYTES_PER_SAMPLE sizeof(SAMPLE_TYPE)
#define BYTES_PER_TICK (BYTES_PER_SAMPLE * SAMPLES_PER_TICK)
#define LENGTH_TICKS (LENGTH_BARS * BAR_TICKS)

#ifdef CAPTURE
#ifndef CAPTURE_FRAMERATE
#define CAPTURE_FRAMERATE 60
#endif
#define LOL(x) #x
#define STR(x) LOL(x)
#define FFMPEG_CAPTURE_INPUT "ffmpeg.exe -y -f rawvideo -vcodec rawvideo -s "\
	STR(XRES) "x" STR(YRES) " -pix_fmt rgb24 -framerate " STR(CAPTURE_FRAMERATE)\
	" -i - -c:v libx264 -crf 18 -preset slow -vf vflip "\
	"capture_" STR(XRES) "x" STR(YRES) ".mp4"
#endif

#pragma data_seg(".raymarch.glsl")
#include "raymarch.h"

#pragma data_seg(".blur_reflection.glsl")
#include "blur_reflection.h"

#pragma data_seg(".blur_reflection2.glsl")
#include "blur_reflection2.h"

#pragma data_seg(".composite.glsl")
#include "composite.h"

#pragma data_seg(".dof_tap.glsl")
#include "dof_tap.h"

#pragma data_seg(".header.glsl")
#include "header_fixed.h"

#pragma data_seg(".post.glsl")
#include "post.h"

#define NOISE_SIZE 256
static unsigned char noise_bytes[4 * NOISE_SIZE * NOISE_SIZE];

//#define OLD_TIMELINE
#define NUM_SIGNALS 32
static float signal[NUM_SIGNALS];

#ifdef OLD_TIMELINE
#pragma data_seg(".timeline.h")
#include "timeline.h"

#pragma data_seg(".timeline_unpacked")

#pragma code_seg(".timeline_updater")
static /*__forceinline*/ void timelineUpdate(float tick, float *TV) {
	float av = 0, bv = 0;
	int at = 0, bt = 1;
	for (int i = 1; i < NUM_SIGNALS; ++i) {
		int key_tick = 0;
		for (int j = 0; j < TIMELINE_KEYPOINTS; ++j) {
			const int dt = timeline_times[j];
			key_tick += dt;
			if (key_tick < tick) {
				if (timeline_signals[j] == i) {
					av = timeline_values[j];
					at = key_tick;
				}
			} else {
				if (timeline_signals[j] == i) {
					bv = timeline_values[j];
					bt = key_tick;
					break;
				}
			}
		} // for all keypoints find signal
		
		TV[i] = (av + (bv - av) * (tick / (bt - at))) * 32.f / 255.f;// -127.f) / 2.f;
		//printf("%.2f ", TV[i]);
	}
	//pritnf("\n");
}
#else
#pragma data_seg(".timeline_packed")
#pragma code_seg(".timeline_updater")

static double fsin(double in) {
	double result;
	_asm { fld in
		fsin
		fstp result
	}
	return result;
}

static float fract(float f) {
	int i;
	_asm {
		fld f
		fisttp i
	}
	return f - (float)i;
}

static float fmod(float f, float d) {
	return fract(f / d) * d;
}

#undef max
#undef min
static float max(float a, float b) { return a > b ? a : b; }
static float min(float a, float b) { return a < b ? a : b; }

static float clamp(float f, float m, float M) {
	return max(min(f, M), m);
}

#define COUNTOF(a) (sizeof(a) / sizeof(*a))

static const char bass_notes[] = {20, 19};

static const char kicks[] = {
	1, 0, 0, 0, 
	0, 1, 0, 0, 
	1, 0, 0, 0, 
	0, 0, 0, 0, 
	1, 0, 0, 0, 
	0, 1, 0, 0, 
	1, 0, 0, 0, 
	0, 0, 1, 0 };

static void timelineUpdate(float tick, float *S) {
	const int itick = tick;
	const int ibar = itick / BAR_TICKS;

	const float scene1 = clamp((tick - BAR_TICKS) / BAR_TICKS, 0.f, 1.f);
	const float scene2 = clamp((tick - 2.f * BAR_TICKS) / BAR_TICKS, 0.f, 1.f);

	S[0] = tick;
	S[1] = 1.f + 10.f * noise_bytes[ibar % NOISE_SIZE] / 255.f;
	S[2] = scene1;
	S[3] = scene2 * fmod(tick, BAR_TICKS) / BAR_TICKS;
	//S[11] = fmod(tick*256., 3.);
	S[10] = kicks[(itick / 4) % COUNTOF(kicks)];
	S[11] = (itick % BAR_TICKS) != (BAR_TICKS - 1);
	S[12] = bass_notes[(ibar / 4) % COUNTOF(bass_notes)] - 15;
	S[13] = (itick + 16) % BAR_TICKS == 0;
	S[14] = 36;
}

#endif

#ifdef SOUND
#pragma data_seg(".wavedata")
static SAMPLE_TYPE sound_buffer[SOUND_SAMPLES];

static float padd(float f, float a) { return fract(f + a); }

static float step(float f, float s) { return f > s;  }
static float psine(float f) { return fsin(f * 3.141693f * 2.f); }
static float noise() {
	static unsigned seed = 0;
	seed = 1013904223ul + seed * 1664525ul;
	return (seed >> 18) / (float)(0x3fff);
}
static float mix(float a, float b, float t) { return a + t * (b - a); }
static float ptri(float v, float p) {
	return ((v < p) ? 1.f - v / p : (v - p) / (1.f - p)) * 2.f - 1.f;
}

static float ntodp(float n) {
	if (n < 0 || n > 64) return 0;
	const float kht = 1.059463f;
	float f = 55.5f / 2.f / SOUND_SAMPLERATE;
	while ((n -= 1.f) >= 0.f) f *= kht;
	return f;
	//return powf(2.f, (stack[sp] - 57.f) / 12.f) * 440.f / context->samplerate;
}

typedef struct Voice {
	int gate_signal, note_signal;
	float (*gen)(struct Voice *v, float *S);
	float p, dp;
	float env, attack, decay;
	float note;
	float last_gatesig;
	float gatetime;
	float p1, dp1;
} Voice;

static float bass(Voice *v, float *S) {
	v->dp1 = .1f / SOUND_SAMPLERATE;
	return clamp(1.7 * ptri(v->p, psine(v->p1) * .3 + .5), -8., .8) * .5;
}

static float kick(Voice *v, float *S) {
	float gt2 = v->gatetime; gt2 *= gt2;
	v->dp = clamp(.0008f / (1.f - gt2), .0001f, .1f);
	return ((psine(v->p) + ptri(v->p, .2)) * 2. + .08 * noise()) * v->gatetime;
}

static float snare(Voice *v, float *S) {
	return (.5 * ptri(v->p, .5) * v->gatetime + noise()) * v->gatetime * v->gatetime;
}

void soundRender() {
	float S[NUM_SIGNALS];
	float kick_phase = 0;
	float kgate = 0;
	float ps10 = 0;

	float bdp = ntodp(12.f);

	Voice voices[] = {
		{11, 12, bass},
		{10, 0, kick},
		{13, 14, snare}
	};

	for (int i = 0; i < SOUND_SAMPLES; ++i) {
		const float tick = (float)i / (float)SAMPLES_PER_TICK;
		timelineUpdate(tick, S);
		float s = 0;

		for (int j = 0; j < COUNTOF(voices); ++j) {
			Voice *v = voices + j;
			if (v->note_signal) v->dp = ntodp(S[v->note_signal]);
			v->p = fract(v->p + v->dp);
			v->p1 = fract(v->p1 + v->dp1);
			s += v->gen(v, S);
			const float gate = S[v->gate_signal];
			if (gate > 0 && v->last_gatesig == 0) {
				v->gatetime = 1.f;
			} else {
				v->gatetime -= .25f / SAMPLES_PER_TICK;
				v->gatetime = max(v->gatetime, 0);
			}

			v->last_gatesig = S[v->gate_signal];
		}

		int delay = 20 * SAMPLES_PER_TICK;
		if (i > delay) {
			//s += .2 * sound_buffer[i - delay];
		}

		sound_buffer[i] = s;

		//sound_buffer[i] = signals[10] * (i % (44100 / 440)) / (float)(44100 / 440);
	}
}
#endif // SOUND

#ifdef NO_CREATESHADERPROGRAMV
FUNCLIST_DO(PFNGLCREATESHADERPROC, CreateShader) \
FUNCLIST_DO(PFNGLSHADERSOURCEPROC, ShaderSource) \
FUNCLIST_DO(PFNGLCOMPILESHADERPROC, CompileShader) \
FUNCLIST_DO(PFNGLCREATEPROGRAMPROC, CreateProgram) \
FUNCLIST_DO(PFNGLATTACHSHADERPROC, AttachShader) \
FUNCLIST_DO(PFNGLLINKPROGRAMPROC, LinkProgram)
#endif

#define FUNCLIST \
  FUNCLIST_DO(PFNGLCREATESHADERPROGRAMVPROC, CreateShaderProgramv) \
  FUNCLIST_DO(PFNGLUSEPROGRAMPROC, UseProgram) \
  FUNCLIST_DO(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
  FUNCLIST_DO(PFNGLUNIFORM1IVPROC, Uniform1iv) \
  FUNCLIST_DO(PFNGLUNIFORM1FVPROC, Uniform1fv) \
  FUNCLIST_DO(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
  FUNCLIST_DO(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
  FUNCLIST_DO(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
  FUNCLIST_DO(PFNGLDRAWBUFFERSPROC, DrawBuffers) \
  FUNCLIST_DO(PFNGLACTIVETEXTUREPROC, ActiveTexture)
#ifndef DEBUG
#define FUNCLIST_DBG
#else
#define FUNCLIST_DBG \
  FUNCLIST_DO(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
  FUNCLIST_DO(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
  FUNCLIST_DO(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)
#endif

#define FUNCLISTS FUNCLIST FUNCLIST_DBG

#ifdef OLD_4KLANG
static SAMPLE_TYPE lpSoundBuffer[MAX_SAMPLES * 2];
#endif

enum {
	Pass_Raymarch,
	Pass_ReflectBlur1, Pass_ReflectBlur2, Pass_ReflectCombine,
	Pass_DofTap, Pass_Post,
	Pass_COUNT
};

enum {
	Tex_Random,
	Tex_RaymarchPrimary, Tex_RaymarchReflect,
	Tex_ReflectBlur1, Tex_ReflectBlur2,
	Tex_Combined,
	Tex_DofNear, Tex_DofFar,
	Tex_COUNT
};

const static GLuint samplers[Tex_COUNT] = {
	0, 1, 2, 3, 4, 5, 6, 7
};
const static GLuint draw_buffers[2] = {
	GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1
};

static GLuint program[Pass_COUNT];
static GLuint texture[Tex_COUNT];
static GLuint fb[Pass_COUNT - 1];

#ifdef CAPTURE
static GLubyte backbufferData[XRES*YRES * 3];
#endif

#ifdef _WIN32
#pragma data_seg(".gl_names")
#define FUNCLIST_DO(T,N) "gl" #N "\0"
static const char gl_names[] =
FUNCLISTS
"\0";

#undef FUNCLIST_DO
#define FUNCLIST_DO(T,N) T N;
static struct {
	FUNCLISTS
} gl;
#undef FUNCLIST_DO

#pragma data_seg(".pfd")
static const PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
	32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 32, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };

#pragma data_seg(".devmode")
static const DEVMODE screenSettings = { {0},
	#if _MSC_VER < 1400
	0,0,148,0,0x001c0000,{0},0,0,0,0,0,0,0,0,0,{0},0,32,XRES,YRES,0,0,      // Visual C++ 6.0
	#else
	0,0,156,0,0x001c0000,{0},0,0,0,0,0,{0},0,32,XRES,YRES,{0}, 0,           // Visual Studio 2005+
	#endif
	#if(WINVER >= 0x0400)
	0,0,0,0,0,0,
	#if (WINVER >= 0x0500) || (_WIN32_WINNT >= 0x0400)
	0,0
	#endif
	#endif
	};

#ifdef SOUND
#pragma data_seg(".wavefmt")
static const WAVEFORMATEX WaveFMT =
{
#ifdef FLOAT_32BIT
	WAVE_FORMAT_IEEE_FLOAT,
#else
	WAVE_FORMAT_PCM,
#endif
	1,                                   // channels
	SOUND_SAMPLERATE,                         // samples per sec
	SOUND_SAMPLERATE * sizeof(SAMPLE_TYPE), // bytes per sec
	sizeof(SAMPLE_TYPE),             // block alignment;
	sizeof(SAMPLE_TYPE) * 8,             // bits per sample
	0                                    // extension not needed
};

#pragma data_seg(".wavehdr")
static WAVEHDR WaveHDR =
{
	(LPSTR)sound_buffer, SOUND_SAMPLES*sizeof(SAMPLE_TYPE),0,0,0,0,0,0
};

#endif // SOUND
#endif /* _WIN32 */

#ifndef _DEBUG
#define GLCHECK()
#define glGetError()
#else
static const char *a__GlPrintError(int error) {
	const char *errstr = "UNKNOWN";
	switch (error) {
	case GL_INVALID_ENUM: errstr = "GL_INVALID_ENUM"; break;
	case GL_INVALID_VALUE: errstr = "GL_INVALID_VALUE"; break;
	case GL_INVALID_OPERATION: errstr = "GL_INVALID_OPERATION"; break;
#ifdef GL_STACK_OVERFLOW
	case GL_STACK_OVERFLOW: errstr = "GL_STACK_OVERFLOW"; break;
#endif
#ifdef GL_STACK_UNDERFLOW
	case GL_STACK_UNDERFLOW: errstr = "GL_STACK_UNDERFLOW"; break;
#endif
	case GL_OUT_OF_MEMORY: errstr = "GL_OUT_OF_MEMORY"; break;
#ifdef GL_TABLE_TOO_LARGE
	case GL_TABLE_TOO_LARGE: errstr = "GL_TABLE_TOO_LARGE"; break;
#endif
	case 1286: errstr = "INVALID FRAMEBUFFER"; break;
	};
	return errstr;
}
static void GLCHECK() {
	const int glerror = glGetError();
	if (glerror != GL_NO_ERROR) {
		MessageBox(NULL, a__GlPrintError(glerror), "GLCHECK", 0);
		ExitProcess(0);
	};
}
#endif /* ATTO_GL_DEBUG */

#pragma code_seg(".compileProgram")
static __forceinline GLuint compileProgram(const char *fragment) {
	const char* sources[2] = { header_glsl, fragment };
#ifdef NO_CREATESHADERPROGRAMV
	const int pid = oglCreateProgram();
	const int fsId = oglCreateShader(GL_FRAGMENT_SHADER);
	oglShaderSource(fsId, 2, sources, 0);
	oglCompileShader(fsId);

#ifdef SHADER_DEBUG
	int result;
	char info[2048];
#define oglGetObjectParameteriv ((PFNGLGETOBJECTPARAMETERIVARBPROC) wglGetProcAddress("glGetObjectParameterivARB"))
#define oglGetInfoLog ((PFNGLGETINFOLOGARBPROC) wglGetProcAddress("glGetInfoLogARB"))
	oglGetObjectParameteriv(fsId, GL_OBJECT_COMPILE_STATUS_ARB, &result);
	oglGetInfoLog(fsId, 2047, NULL, (char*)info);
	if (!result)
	{
		MessageBox(NULL, info, "COMPILE", 0x00000000L);
		ExitProcess(0);
	}
#endif

	oglAttachShader(pid, fsId);
	oglLinkProgram(pid);

#else
	const GLuint pid = oglCreateShaderProgramv(GL_FRAGMENT_SHADER, 2, sources);
#endif

#ifdef SHADER_DEBUG
	{
		int result;
		char info[2048];
#define oglGetObjectParameteriv ((PFNGLGETOBJECTPARAMETERIVARBPROC) wglGetProcAddress("glGetObjectParameterivARB"))
#define oglGetInfoLog ((PFNGLGETINFOLOGARBPROC) wglGetProcAddress("glGetInfoLogARB"))
		oglGetObjectParameteriv(pid, GL_OBJECT_LINK_STATUS_ARB, &result);
		oglGetInfoLog(pid, 2047, NULL, (char*)info);
		if (!result)
		{
			MessageBox(NULL, info, "LINK", 0x00000000L);
			ExitProcess(0);
		}
	}
#endif
	return pid;
}

#pragma code_seg(".initTexture")
static /*__forceinline*/ void initTexture(GLuint tex, int w, int h, int comp, int type, void *data) {
	glBindTexture(GL_TEXTURE_2D, tex);
	GLCHECK();
	glTexImage2D(GL_TEXTURE_2D, 0, comp, w, h, 0, GL_RGBA, type, data);
	GLCHECK();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

#pragma code_seg(".initFb")
static /*__forceinline*/ void initFb(GLuint fb, GLuint tex1, GLuint tex2) {
	oglBindFramebuffer(GL_FRAMEBUFFER, fb);
	GLCHECK();
	oglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex1, 0);
	GLCHECK();
	//if (tex2 > 0)
		oglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex2, 0);
	GLCHECK();
}

static float signals[NUM_SIGNALS];

#pragma code_seg(".paint")
static void paint(GLuint prog, GLuint dst_fb, int out_bufs) {
	oglUseProgram(prog);
	GLCHECK();
	//glBindTexture(GL_TEXTURE_2D, src_tex);
	oglBindFramebuffer(GL_FRAMEBUFFER, dst_fb);
	GLCHECK();
	// wastes bytes
	if (dst_fb) oglDrawBuffers(out_bufs, draw_buffers);
	GLCHECK();
	oglUniform1iv(oglGetUniformLocation(prog, "S"), Tex_COUNT, samplers);
	glGetError();
	oglUniform1fv(oglGetUniformLocation(prog, "F"), NUM_SIGNALS, signals);
	glGetError();
#if defined(CAPTURE) && defined(TILED)
	{
		int x, y;
		for (y = 0; y < YRES; y += 64)
			for (x = 0; x < XRES; x += 64) {
				glRectf(
					x * 2.f / XRES - 1,
					y * 2.f / YRES - 1,
					(x + 64) * 2.f / XRES - 1,
					(y + 64) * 2.f / YRES - 1);
				glFinish();
			}
	}
#else
	glRects(-1, -1, 1, 1);
#endif
	GLCHECK();
}

#pragma code_seg(".introInit")
static __forceinline void introInit() {
	unsigned seed = 0;
	for (int i = 0; i < 4 * NOISE_SIZE * NOISE_SIZE; ++i) {
		seed = 1013904223ul + seed * 1664525ul;
		noise_bytes[i] = (seed >> 18);
	}

	glGenTextures(Tex_COUNT, texture);
	GLCHECK();
	oglGenFramebuffers(Pass_COUNT - 1, fb);
	GLCHECK();

	initTexture(texture[Tex_Random], NOISE_SIZE, NOISE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, noise_bytes);
	oglActiveTexture(GL_TEXTURE1);
	initTexture(texture[Tex_RaymarchPrimary], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE2);
	initTexture(texture[Tex_RaymarchReflect], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE3);
	initTexture(texture[Tex_ReflectBlur1], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE4);
	initTexture(texture[Tex_ReflectBlur2], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE5);
	initTexture(texture[Tex_Combined], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE6);
	initTexture(texture[Tex_DofNear], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);
	oglActiveTexture(GL_TEXTURE7);
	initTexture(texture[Tex_DofFar], XRES, YRES, GL_RGBA16F, GL_FLOAT, 0);

	initFb(fb[Pass_Raymarch], texture[Tex_RaymarchPrimary], texture[Tex_RaymarchReflect]);
	initFb(fb[Pass_ReflectBlur1], texture[Tex_ReflectBlur1], 0);
	initFb(fb[Pass_ReflectBlur2], texture[Tex_ReflectBlur2], 0);
	initFb(fb[Pass_ReflectCombine], texture[Tex_Combined], 0);
	initFb(fb[Pass_DofTap], texture[Tex_DofNear], texture[Tex_DofFar]);

	program[Pass_Raymarch] = compileProgram(raymarch_glsl);
	program[Pass_ReflectBlur1] = compileProgram(blur_reflection_glsl);
	program[Pass_ReflectBlur2] = compileProgram(blur_reflection2_glsl);
	program[Pass_ReflectCombine] = compileProgram(composite_glsl);
	program[Pass_DofTap] = compileProgram(dof_tap_glsl);
	program[Pass_Post] = compileProgram(post_glsl);
}

#pragma code_seg(".introPaint")
static __forceinline void introPaint(float time_tick) {
	timelineUpdate(time_tick, signals);
	signals[0] = time_tick / BAR_TICKS;
	paint(program[Pass_Raymarch], fb[Pass_Raymarch], 2);
	paint(program[Pass_ReflectBlur1], fb[Pass_ReflectBlur1], 1);
	paint(program[Pass_ReflectBlur2], fb[Pass_ReflectBlur2], 1);
	paint(program[Pass_ReflectCombine], fb[Pass_ReflectCombine], 1);
	paint(program[Pass_DofTap], fb[Pass_DofTap], 2);
	paint(program[Pass_Post], 0, 1);
}

#ifdef _WIN32

#ifdef _DEBUG
void checkResult(int result) {
	if (result != 0)
		ExitProcess(0);
}
#endif

#pragma code_seg(".entry")
void entrypoint(void) {
#ifdef FULLSCREEN
	ChangeDisplaySettings(&screenSettings, CDS_FULLSCREEN);
	ShowCursor(0);
	HDC hDC = GetDC(CreateWindow((LPCSTR)0xC018, 0, WS_POPUP | WS_VISIBLE, 0, 0, XRES, YRES, 0, 0, 0, 0));
#else
	HDC hDC = GetDC(CreateWindow("static", 0, WS_POPUP | WS_VISIBLE, 0, 0, XRES, YRES, 0, 0, 0, 0));
#endif

	// initalize opengl
	SetPixelFormat(hDC, ChoosePixelFormat(hDC, &pfd), &pfd);
	wglMakeCurrent(hDC, wglCreateContext(hDC));

	{
		const char *next_gl_func = gl_names;
		void **funcptr = &gl;
		while (next_gl_func[0] != '\0') {
			*funcptr = wglGetProcAddress(next_gl_func);
#ifdef DEBUG_FUNCLOAD
			if (!*funcptr) {
				\
					MessageBox(NULL, next_gl_func, "wglGetProcAddress", 0x00000000L); \
			}
#endif
			++funcptr;
			while (*(next_gl_func++) != '\0');
		}
	}

	introInit();

#ifdef SOUND
#ifdef _DEBUG
#define CHECK(f) checkResult(f)
#else
#define CHECK(f) (f)
#endif
	// initialize sound
	HWAVEOUT hWaveOut;
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)soundRender, sound_buffer, 0, 0);
	//soundRender(sound_buffer);
	CHECK(waveOutOpen(&hWaveOut, WAVE_MAPPER, &WaveFMT, NULL, 0, CALLBACK_NULL));
	CHECK(waveOutPrepareHeader(hWaveOut, &WaveHDR, sizeof(WaveHDR)));
	CHECK(waveOutWrite(hWaveOut, &WaveHDR, sizeof(WaveHDR)));
#else
	const int start = timeGetTime();
#endif

	// play intro
	do {
#ifdef SOUND
		MMTIME mmtime;
		mmtime.wType = TIME_BYTES;
		CHECK(waveOutGetPosition(hWaveOut, &mmtime, sizeof(mmtime)));
		const float time_ticks = (float)mmtime.u.cb / (BYTES_PER_TICK);
#else
		const int time_ms = timeGetTime() - start;
#endif

		introPaint(time_ticks);
		SwapBuffers(hDC);

		/* hide cursor properly */
		PeekMessageA(0, 0, 0, 0, PM_REMOVE);
		if (time_ticks >= LENGTH_TICKS) break;
	} while (!GetAsyncKeyState(VK_ESCAPE));
		// && MMTime.u.sample < MAX_SAMPLES);

	#ifdef CLEANDESTROY
	sndPlaySound(0,0);
	ChangeDisplaySettings( 0, 0 );
	ShowCursor(1);
	#endif

#ifdef CAPTURE
	fclose(captureStream);
#endif

	ExitProcess(0);
}
#elif defined(__linux__)
/* cc -Wall -Wno-unknown-pragmas `pkg-config --cflags --libs sdl` -lGL nwep.c -o nwep */
#include <SDL.h>
#include <pthread.h>

#ifndef NO_AUDIO
void __4klang_render(void*);

static int audio_cursor = 0;

static void audioPlay(void *userdata, Uint8 *stream, int len) {
	short *p = (short*)stream;
	int i;
	len /= sizeof(short);
	for (i = 0; i < len; ++i) {
		audio_cursor = (audio_cursor + 1) % (MAX_SAMPLES * 2);
		p[i] = lpSoundBuffer[audio_cursor] * 32767.f;
	}
}

static SDL_AudioSpec as = {SAMPLE_RATE, 0x8010, 2, 0, 0, 0, 0, audioPlay};
#endif /* AUDIO */

#ifndef COMPACT
int main() {
#else
void _start() {
#endif
#ifndef NO_AUDIO
	pthread_t audio_thread;
	pthread_create(&audio_thread, 0, (void *(*)(void*))__4klang_render, lpSoundBuffer);
	//__4klang_render(lpSoundBuffer);
#endif
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#ifdef FULLSCREEN
#undef FULLSCREEN
#define FULLSCREEN SDL_FULLSCREEN
	SDL_ShowCursor(0);
#endif
	SDL_SetVideoMode(XRES, YRES, 32, SDL_OPENGL | FULLSCREEN);
	glViewport(0, 0, XRES, YRES);
	introInit();

#ifndef CAPTURE
#ifndef NO_AUDIO
	SDL_OpenAudio(&as, 0);
	SDL_PauseAudio(0);
#endif
	const uint32_t start = SDL_GetTicks();
	for(;;) {
		const uint32_t now = SDL_GetTicks();
#else
	const uint32_t global_start = SDL_GetTicks();
	const uint32_t start = 0;
	const uint32_t total_frames = INTRO_LENGTH * CAPTURE_FRAMERATE / 1000;
	uint32_t frame_start = SDL_GetTicks();
	int frame = 0;
	for (;;) {
		const uint32_t now = ((frame++) * 1000.f) / CAPTURE_FRAMERATE;
#endif
		SDL_Event e;
		SDL_PollEvent(&e);
		if ((e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) || now >= INTRO_LENGTH) break;
		introPaint((now - start) / 1000.f);

#ifdef CAPTURE
		const uint32_t frame_end = SDL_GetTicks();
		const uint32_t total_time = frame_end - global_start;
		glReadPixels(0, 0, XRES, YRES, GL_RGB, GL_UNSIGNED_BYTE, backbufferData);
		write(1, backbufferData, XRES * YRES * 3);
		fprintf(stderr, "frame %d/%d: %dms/frame, total: %ds/%llds, MBytes: %lld/%lld\n",
			frame, total_frames, frame_end - frame_start,
			total_time / 1000, (unsigned long long)total_time * total_frames / frame / 1000,
			((unsigned long long)frame * XRES * YRES * 3) / 1024 / 1024,
			((unsigned long long)total_frames * XRES * YRES * 3) / 1024 / 1024);
		usleep(1000);
		frame_start = frame_end;
#endif

		SDL_GL_SwapBuffers();
	}

#ifdef COMPACT
	asm ( \
		"xor %eax,%eax\n" \
		"inc %eax\n" \
		"int $0x80\n" \
	);
#else
	SDL_Quit();
	return 0;
#endif
}
#else
#error Not ported
#endif
