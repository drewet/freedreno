/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wrap.h"

static int fd = -1;
static unsigned int gpu_id;

void __assert_fail (const char *__assertion, const char *__file,
			   unsigned int __line, const char *__function)
{
	printf("OH CRAP!!!\n");
}

char *getcwd(char *buf, size_t size);

int __android_log_print(int prio, const char *tag,  const char *fmt, ...);
int vsprintf(char *str, const char *format, va_list ap);

static char tracebuf[1024], *tracebufp = tracebuf;

int wrap_printf(const char *format, ...)
{
	int n;
	va_list args;

	va_start(args, format);
	n = vsprintf(tracebufp, format, args);
	va_end(args);

	if ((tracebufp[n] == '\n') || ((tracebufp - tracebuf) > 512)) {
		__android_log_print(5, "WRAP", "%s (%u)", tracebuf, (tracebufp - tracebuf));
		tracebufp = tracebuf;
	} else {
		tracebufp += n;
	}

	return n;
}

unsigned getpid(void);

void rd_start(const char *name, const char *fmt, ...)
{
	char buf[256];
	static int cnt = 0;
	int n = cnt++;
	const char *testnum;
	va_list  args;

	testnum = getenv("TESTNUM");
	if (testnum)
		n = strtol(testnum, NULL, 0);

	sprintf(buf, "trace-%04u.rd", name, getpid());

	fd = open(buf, O_WRONLY| O_TRUNC | O_CREAT, 0644);

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	rd_write_section(RD_TEST, buf, strlen(buf));

	if (gpu_id) {
		/* no guarantee that blob driver will again get devinfo property,
		 * so we could miss the GPU_ID section in the new rd file.. so
		 * just hack around it:
		 */
		rd_write_section(RD_GPU_ID, &gpu_id, sizeof(gpu_id));
	}
}

void rd_end(void)
{
	close(fd);
	fd = -1;
}

volatile int*  __errno( void );
#undef errno
#define errno (*__errno())

static void rd_write(const void *buf, int sz)
{
	uint8_t *cbuf = buf;
	while (sz > 0) {
		int ret = write(fd, cbuf, sz);
		if (ret < 0) {
			printf("error: %d (%s)\n", ret, strerror(errno));
			printf("fd=%d, buf=%p, sz=%d\n", fd, buf, sz);
			exit(-1);
		}
		cbuf += ret;
		sz -= ret;
	}
}

void rd_write_section(enum rd_sect_type type, const void *buf, int sz)
{
	uint32_t val = ~0;

	if (fd == -1) {
		rd_start("unknown", "unknown");
		printf("opened rd, %d\n", fd);
	}

	if (type == RD_GPU_ID) {
		gpu_id = *(unsigned int *)buf;
	}

	rd_write(&val, 4);
	rd_write(&val, 4);

	rd_write(&type, 4);
	val = ALIGN(sz, 4);
	rd_write(&val, 4);
	rd_write(buf, sz);

	val = 0;
	rd_write(&val, ALIGN(sz, 4) - sz);

	if (wrap_safe())
		fsync(fd);
}

/* in safe mode, sync log file frequently, and insert delays before/after
 * issueibcmds.. useful when we are crashing things and want to be sure to
 * capture as much of the log as possible
 */
unsigned int wrap_safe(void)
{
	static unsigned int val = -1;
	if (val == -1) {
		const char *str = getenv("WRAP_SAFE");
		val = str ? strtol(str, NULL, 0) : 0;
	}
	return val;
}

/* if non-zero, emulate a different gpu-id.  The issueibcmds will be stubbed
 * so we don't actually submit cmds to the gpu.  This is useful to generate
 * cmdstream dumps for different gpu versions for comparision.
 */
unsigned int wrap_gpu_id(void)
{
	static unsigned int val = -1;
	if (val == -1) {
		const char *str = getenv("WRAP_GPU_ID");
		val = str ? strtol(str, NULL, 0) : 0;
	}
	return val;
}

/* defaults to zero if $WRAP_GPU_ID does not end in a ".%d".. */
unsigned int wrap_gpu_id_patchid(void)
{
	static unsigned int val = -1;
	if (val == -1) {
		char *endptr = NULL;
		const char *str = getenv("WRAP_GPU_ID");
		if (str) {
			unsigned int gpuid = strtol(str, &endptr, 0);
			if (endptr[0] == '.') {
				val = strtol(endptr + 1, NULL, 0);
			}
		} else {
			val = 0;
		}
	}
	return val;
}

unsigned int wrap_gmem_size(void)
{
	static unsigned int val = -1;
	if (val == -1) {
		const char *str = getenv("WRAP_GMEM_SIZE");
		val = str ? strtol(str, NULL, 0) : 0;
	}
	return val;
}

void * _dlsym_helper(const char *name)
{
	static void *libc_dl;
#ifdef BIONIC
	static void *libc2d2_dl;
#endif
	void *func;

#ifndef BIONIC
	if (!libc_dl)
		libc_dl = dlopen("/lib/arm-linux-gnueabihf/libc-2.15.so", RTLD_LAZY);
	if (!libc_dl)
		libc_dl = dlopen("/lib/libc-2.16.so", RTLD_LAZY);
#endif
	if (!libc_dl)
		libc_dl = dlopen("libc.so", RTLD_LAZY);

	if (!libc_dl) {
		printf("Failed to dlopen libc: %s\n", dlerror());
		exit(-1);
	}

#if 0
	if (!libc2d2_dl)
		libc2d2_dl = dlopen("libC2D2.so", RTLD_LAZY);

	if (!libc2d2_dl) {
		printf("Failed to dlopen c2d2: %s\n", dlerror());
		exit(-1);
	}
#endif

	func = dlsym(libc_dl, name);

#ifdef BIONIC
	if (!func)
		func = dlsym(libc2d2_dl, name);
#endif

	if (!func) {
		printf("Failed to find %s: %s\n", name, dlerror());
		exit(-1);
	}

	return func;
}
