/*
	Framebuffer acceleration hardware abstraction functions.
	The hardware dependent framebuffer acceleration functions for STi chips
	are represented in this class.

	(C) 2017-2018 Stefan Seyfried

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <driver/fb_generic.h>
#include <driver/fb_accel.h>

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <memory.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>

#include <linux/kd.h>

#include <stdlib.h>

#include <linux/stmfb.h>
#include <bpamem.h>

#include <driver/abstime.h>
#include <system/set_threadname.h>

/* note that it is *not* enough to just change those values */
#define DEFAULT_XRES 1280
#define DEFAULT_YRES 720
#define DEFAULT_BPP  32

#define LOGTAG "[fb_accel_sti] "
#if 0
static int bpafd = -1;
#endif
#define BB_DIMENSION ( DEFAULT_XRES * DEFAULT_YRES )
static size_t lfb_sz = 1920 * 1080;	/* offset from fb start in 'pixels' */
static size_t lbb_off = lfb_sz * sizeof(fb_pixel_t);	/* same in bytes */
static int backbuf_sz = BB_DIMENSION * sizeof(fb_pixel_t); /* size of blitting buffer in bytes */
static size_t backbuf_off = lbb_off + backbuf_sz;

void CFbAccelSTi::waitForIdle(const char *)
{
#if 0	/* blits too often and does not seem to be necessary */
	blit_mutex.lock();
	if (blit_pending)
	{
		blit_mutex.unlock();
		_blit();
		return;
	}
	blit_mutex.unlock();
#endif
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	ioctl(fd, STMFBIO_SYNC_BLITTER);
}

CFbAccelSTi::CFbAccelSTi()
{
	fb_name = "STx7xxx framebuffer";
}

void CFbAccelSTi::init(const char * const)
{
	blit_thread = false;
	CFrameBuffer::init();
	if (lfb == NULL) {
		printf(LOGTAG "CFrameBuffer::init() failed.\n");
		return; /* too bad... */
	}
	available = fix.smem_len;
	printf(LOGTAG "%dk video mem\n", available / 1024);
	memset(lfb, 0, available);

	lbb = lfb;	/* the memory area to draw to... */
	if (available < 15*1024*1024)
	{
		/* for old installations that did not upgrade their module config
		 * it will still work good enough to display the message below */
		fprintf(stderr, "[neutrino] WARNING: not enough framebuffer memory available!\n");
		fprintf(stderr, "[neutrino]          I need at least 15MB.\n");
		FILE *f = fopen("/tmp/infobar.txt", "w");
		if (f) {
			fprintf(f, "NOT ENOUGH FRAMEBUFFER MEMORY!");
			fclose(f);
		}
		lfb_sz = 0;
		lbb_off = 0;
	}
	lbb = lfb + lfb_sz;
	backbuffer = lbb + BB_DIMENSION;
#if 0
	bpafd = open("/dev/bpamem0", O_RDWR | O_CLOEXEC);
	if (bpafd < 0)
	{
		fprintf(stderr, "[neutrino] FB: cannot open /dev/bpamem0: %m\n");
		return;
	}
	backbuf_sz = 1280 * 720 * sizeof(fb_pixel_t);
	BPAMemAllocMemData bpa_data;
	bpa_data.bpa_part = (char *)"LMI_VID";
	bpa_data.mem_size = backbuf_sz;
	int res;
	res = ioctl(bpafd, BPAMEMIO_ALLOCMEM, &bpa_data);
	if (res)
	{
		fprintf(stderr, "[neutrino] FB: cannot allocate from bpamem: %m\n");
		fprintf(stderr, "backbuf_sz: %d\n", backbuf_sz);
		close(bpafd);
		bpafd = -1;
		return;
	}
	close(bpafd);

	char bpa_mem_device[30];
	sprintf(bpa_mem_device, "/dev/bpamem%d", bpa_data.device_num);
	bpafd = open(bpa_mem_device, O_RDWR | O_CLOEXEC);
	if (bpafd < 0)
	{
		fprintf(stderr, "[neutrino] FB: cannot open secondary %s: %m\n", bpa_mem_device);
		return;
	}

	backbuffer = (fb_pixel_t *)mmap(0, bpa_data.mem_size, PROT_WRITE|PROT_READ, MAP_SHARED, bpafd, 0);
	if (backbuffer == MAP_FAILED)
	{
		fprintf(stderr, "[neutrino] FB: cannot map from bpamem: %m\n");
		ioctl(bpafd, BPAMEMIO_FREEMEM);
		close(bpafd);
		bpafd = -1;
		return;
	}
#endif

	/* start the autoblit-thread (run() function) */
	OpenThreads::Thread::start();
};

CFbAccelSTi::~CFbAccelSTi()
{
	if (blit_thread)
	{
		blit_thread = false;
		blit(); /* wakes up the thread */
		OpenThreads::Thread::join();
	}
#if 0
	if (backbuffer)
	{
		fprintf(stderr, LOGTAG "unmap backbuffer\n");
		munmap(backbuffer, backbuf_sz);
	}
	if (bpafd != -1)
	{
		fprintf(stderr, LOGTAG "BPAMEMIO_FREEMEM\n");
		ioctl(bpafd, BPAMEMIO_FREEMEM);
		close(bpafd);
	}
#endif
	if (lfb)
		munmap(lfb, available);
	if (fd > -1)
		close(fd);
}

void CFbAccelSTi::paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col)
{
	if (dx <= 0 || dy <= 0)
		return;

	// The STM blitter introduces considerable overhead probably not worth for single lines. --martii
	if (dx == 1) {
		waitForIdle();
		fb_pixel_t *fbs = getFrameBufferPointer() + (DEFAULT_XRES * y) + x;
		fb_pixel_t *fbe = fbs + DEFAULT_XRES * dy;
		while (fbs < fbe) {
			*fbs = col;
			fbs += DEFAULT_XRES;
		}
		mark(x , y, x + 1, y + dy);
		return;
	}
	if (dy == 1) {
		waitForIdle();
		fb_pixel_t *fbs = getFrameBufferPointer() + (DEFAULT_XRES * y) + x;
		fb_pixel_t *fbe = fbs + dx;
		while (fbs < fbe)
			*fbs++ = col;
		mark(x , y, x + dx, y + 1);
		return;
	}

	/* function has const parameters, so copy them here... */
	int width = dx;
	int height = dy;
	int xx = x;
	int yy = y;
	/* maybe we should just return instead of fixing this up... */
	if (x < 0) {
		fprintf(stderr, "[neutrino] fb::%s: x < 0 (%d)\n", __func__, x);
		width += x;
		if (width <= 0)
			return;
		xx = 0;
	}

	if (y < 0) {
		fprintf(stderr, "[neutrino] fb::%s: y < 0 (%d)\n", __func__, y);
		height += y;
		if (height <= 0)
			return;
		yy = 0;
	}

	int right = xx + width;
	int bottom = yy + height;

	if (right > (int)xRes) {
		if (xx >= (int)xRes) {
			fprintf(stderr, "[neutrino] fb::%s: x >= xRes (%d > %d)\n", __func__, xx, xRes);
			return;
		}
		fprintf(stderr, "[neutrino] fb::%s: x+w > xRes! (%d+%d > %d)\n", __func__, xx, width, xRes);
		right = xRes;
	}
	if (bottom > (int)yRes) {
		if (yy >= (int)yRes) {
			fprintf(stderr, "[neutrino] fb::%s: y >= yRes (%d > %d)\n", __func__, yy, yRes);
			return;
		}
		fprintf(stderr, "[neutrino] fb::%s: y+h > yRes! (%d+%d > %d)\n", __func__, yy, height, yRes);
		bottom = yRes;
	}

	STMFBIO_BLT_DATA bltData;
	memset(&bltData, 0, sizeof(STMFBIO_BLT_DATA));

	bltData.operation  = BLT_OP_FILL;
	bltData.dstOffset  = lbb_off;
	bltData.dstPitch   = stride;

	bltData.dst_left   = xx;
	bltData.dst_top    = yy;
	bltData.dst_right  = right;
	bltData.dst_bottom = bottom;

	bltData.dstFormat  = SURF_ARGB8888;
	bltData.srcFormat  = SURF_ARGB8888;
	bltData.dstMemBase = STMFBGP_FRAMEBUFFER;
	bltData.srcMemBase = STMFBGP_FRAMEBUFFER;
	bltData.colour     = col;

	mark(xx, yy, bltData.dst_right, bltData.dst_bottom);
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	if (ioctl(fd, STMFBIO_BLT, &bltData ) < 0)
		fprintf(stderr, "blitRect FBIO_BLIT: %m x:%d y:%d w:%d h:%d s:%d\n", xx,yy,width,height,stride);
	//blit();
}

/* width / height => source surface   *
 * xoff / yoff    => target position  *
 * xp / yp        => offset in source */
void CFbAccelSTi::blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp, uint32_t yp, bool transp)
{
	int x, y, dw, dh, bottom;
	x = xoff;
	y = yoff;
	dw = width - xp;
	dh = height - yp;
	bottom = height + yp;

	size_t mem_sz = width * bottom * sizeof(fb_pixel_t);
	/* we can blit anything from [ backbuffer <--> backbuffer + backbuf_sz ]
	 * if the source is outside this, then it will be memmove()d to start of backbuffer */
	void *tmpbuff = backbuffer;
	if ((fbbuff >= backbuffer) && (uint8_t *)fbbuff + mem_sz <= (uint8_t *)backbuffer + backbuf_sz)
		tmpbuff = fbbuff;
	unsigned long ulFlags = 0;
	if (!transp) /* transp == false (default): use transparency from source alphachannel */
		ulFlags = BLT_OP_FLAGS_BLEND_SRC_ALPHA|BLT_OP_FLAGS_BLEND_DST_MEMORY; // we need alpha blending
#if 0
	STMFBIO_BLT_EXTERN_DATA blt_data;
	memset(&blt_data, 0, sizeof(STMFBIO_BLT_EXTERN_DATA));
	blt_data.operation  = BLT_OP_COPY;
	blt_data.ulFlags    = ulFlags;
	blt_data.srcOffset  = 0;
	blt_data.srcPitch   = width * 4;
	blt_data.dstOffset  = lbb_off;
	blt_data.dstPitch   = stride;
	blt_data.src_left   = xp;
	blt_data.src_top    = yp;
	blt_data.src_right  = width;
	blt_data.src_bottom = bottom;
	blt_data.dst_left   = x;
	blt_data.dst_top    = y;
	blt_data.dst_right  = x + dw;
	blt_data.dst_bottom = y + dh;
	blt_data.srcFormat  = SURF_ARGB8888;
	blt_data.dstFormat  = SURF_ARGB8888;
	blt_data.srcMemBase = (char *)tmpbuff;
	blt_data.dstMemBase = (char *)lfb;
	blt_data.srcMemSize = mem_sz;
	blt_data.dstMemSize = stride * yRes + lbb_off;

	mark(x, y, blt_data.dst_right, blt_data.dst_bottom);
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	ioctl(fd, STMFBIO_SYNC_BLITTER);
	if (fbbuff != tmpbuff)
		memmove(backbuffer, fbbuff, mem_sz);
	// icons are so small that they will still be in cache
	msync(backbuffer, backbuf_sz, MS_SYNC);

	if (ioctl(fd, STMFBIO_BLT_EXTERN, &blt_data) < 0) {
		perror(LOGTAG "blit2FB STMFBIO_BLT_EXTERN");
		fprintf(stderr, "fbbuff %p tmp %p back %p width %u height %u xoff %u yoff %u xp %u yp %u dw %d dh %d\n",
				fbbuff, tmpbuff, backbuffer, width, height, xoff, yoff, xp, yp, dw, dh);
		fprintf(stderr, "left: %d top: %d right: %d bottom: %d off: %ld pitch: %ld mem: %ld\n",
				blt_data.src_left, blt_data.src_top, blt_data.src_right, blt_data.src_bottom,
				blt_data.srcOffset, blt_data.srcPitch, blt_data.srcMemSize);
	}
#else
	STMFBIO_BLT_DATA blt_data;
	memset(&blt_data, 0, sizeof(STMFBIO_BLT_DATA));
	blt_data.operation  = BLT_OP_COPY;
	blt_data.ulFlags    = ulFlags;
	blt_data.srcOffset  = backbuf_off;
	blt_data.srcPitch   = width * 4;
	blt_data.dstOffset  = lbb_off;
	blt_data.dstPitch   = stride;
	blt_data.src_left   = xp;
	blt_data.src_top    = yp;
	blt_data.src_right  = width;
	blt_data.src_bottom = bottom;
	blt_data.dst_left   = x;
	blt_data.dst_top    = y;
	blt_data.dst_right  = x + dw;
	blt_data.dst_bottom = y + dh;
	blt_data.srcFormat  = SURF_ARGB8888;
	blt_data.dstFormat  = SURF_ARGB8888;
	blt_data.srcMemBase = STMFBGP_FRAMEBUFFER;
	blt_data.dstMemBase = STMFBGP_FRAMEBUFFER;

	mark(x, y, blt_data.dst_right, blt_data.dst_bottom);
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	ioctl(fd, STMFBIO_SYNC_BLITTER);
	if (fbbuff != tmpbuff)
		memmove(backbuffer, fbbuff, mem_sz);
	// icons are so small that they will still be in cache
	msync(backbuffer, backbuf_sz, MS_SYNC);
	if (ioctl(fd, STMFBIO_BLT, &blt_data ) < 0)
		perror(LOGTAG "blit2FB STMFBIO_BLT");
#endif
	return;
}

#define BLIT_INTERVAL_MIN 40
#define BLIT_INTERVAL_MAX 250
void CFbAccelSTi::run()
{
	printf(LOGTAG "::run start\n");
	int64_t last_blit = 0;
	blit_pending = false;
	blit_thread = true;
	set_threadname("stifb::autoblit");
	while (blit_thread) {
		blit_mutex.lock();
		blit_cond.wait(&blit_mutex, blit_pending ? BLIT_INTERVAL_MIN : BLIT_INTERVAL_MAX);
		blit_mutex.unlock();

		int64_t now = time_monotonic_ms();
		int64_t diff = now - last_blit;
		if (diff < BLIT_INTERVAL_MIN)
		{
			blit_pending = true;
			//printf(LOGTAG "::run: skipped, time %" PRId64 "\n", diff);
		}
		else
		{
			blit_pending = false;
			_blit();
			last_blit = now;
		}
	}
	printf(LOGTAG "::run end\n");
}

void CFbAccelSTi::blit()
{
	//printf(LOGTAG "::blit\n");
#if 0
	/* After 99ff4857 "change time_monotonic_ms() from time_t to int64_t"
	 * this is no longer needed. And it leads to rendering errors.
	 * Safest would be "blit_mutex.timedlock(timeout)", but that does not
	 * exist... */
	int status = blit_mutex.trylock();
	if (status) {
		printf(LOGTAG "::blit trylock failed: %d (%s)\n", status,
				(status > 0) ? strerror(status) : strerror(errno));
		return;
	}
#else
	blit_mutex.lock();
#endif
	blit_cond.signal();
	blit_mutex.unlock();
}

void CFbAccelSTi::_blit()
{
#if 0
	static int64_t last = 0;
	int64_t now = time_monotonic_ms();
	printf("%s %" PRId64 "\n", __func__, now - last);
	last = now;
#endif
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	const int srcXa = 0;
	const int srcYa = 0;
	int srcXb = xRes;
	int srcYb = yRes;
	STMFBIO_BLT_DATA  bltData;
	memset(&bltData, 0, sizeof(STMFBIO_BLT_DATA));

	bltData.operation  = BLT_OP_COPY;
	//bltData.ulFlags  = BLT_OP_FLAGS_BLEND_SRC_ALPHA | BLT_OP_FLAGS_BLEND_DST_MEMORY; // we need alpha blending
	// src
	bltData.srcOffset  = lbb_off;
	bltData.srcPitch   = stride;

	bltData.src_left   = srcXa;
	bltData.src_top    = srcYa;
	bltData.src_right  = srcXb;
	bltData.src_bottom = srcYb;

	bltData.srcFormat = SURF_BGRA8888;
	bltData.srcMemBase = STMFBGP_FRAMEBUFFER;

	/* calculate dst/blit factor */
	fb_var_screeninfo s;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &s) == -1)
		perror("CFbAccel <FBIOGET_VSCREENINFO>");

	const int desXa = 0;
	const int desYa = 0;
	int desXb = s.xres;
	int desYb = s.yres;

	/* dst */
	bltData.dstOffset  = 0;
	bltData.dstPitch   = s.xres * 4;

	bltData.dst_left   = desXa;
	bltData.dst_top    = desYa;
	bltData.dst_right  = desXb;
	bltData.dst_bottom = desYb;

	bltData.dstFormat = SURF_BGRA8888;
	bltData.dstMemBase = STMFBGP_FRAMEBUFFER;

	//printf("CFbAccelSTi::blit: sx:%d sy:%d sxe:%d sye: %d dx:%d dy:%d dxe:%d dye:%d\n", srcXa, srcYa, srcXb, srcYb, desXa, desYa, desXb, desYb);
	if ((bltData.dst_right > s.xres) || (bltData.dst_bottom > s.yres))
		printf(LOGTAG "blit: values out of range desXb:%d desYb:%d\n",
			bltData.dst_right, bltData.dst_bottom);

	if(ioctl(fd, STMFBIO_SYNC_BLITTER) < 0)
		perror(LOGTAG "blit ioctl STMFBIO_SYNC_BLITTER 1");
	msync(lbb, xRes * 4 * yRes, MS_SYNC);
	if (ioctl(fd, STMFBIO_BLT, &bltData ) < 0)
		perror(LOGTAG "STMFBIO_BLT");
	if(ioctl(fd, STMFBIO_SYNC_BLITTER) < 0)
		perror(LOGTAG "blit ioctl STMFBIO_SYNC_BLITTER 2");
}

void CFbAccelSTi::mark(int, int, int, int)
{
}

/* wrong name... */
int CFbAccelSTi::setMode(unsigned int, unsigned int, unsigned int)
{
	/* it's all fake... :-) */
	xRes = screeninfo.xres = screeninfo.xres_virtual = DEFAULT_XRES;
	yRes = screeninfo.yres = screeninfo.yres_virtual = DEFAULT_YRES;
	bpp  = screeninfo.bits_per_pixel = DEFAULT_BPP;
	stride = screeninfo.xres * screeninfo.bits_per_pixel / 8;
	swidth = screeninfo.xres;
	return 0;
}

fb_pixel_t *CFbAccelSTi::getBackBufferPointer() const
{
	return backbuffer;
}

/* original interfaceL: 1 == pixel alpha, 2 == global alpha premultiplied */
void CFbAccelSTi::setBlendMode(uint8_t mode)
{
	/* mode = 1 => reset to no extra transparency */
	if (mode == 1)
		setBlendLevel(0);
}

/* level = 100 -> transparent, level = 0 -> nontransperent */
void CFbAccelSTi::setBlendLevel(int level)
{
	struct stmfbio_var_screeninfo_ex v;
	memset(&v, 0, sizeof(v));
	/* set to 0 already...
	 v.layerid = 0;
	 v.activate = STMFBIO_ACTIVATE_IMMEDIATE; // == 0
	 v.premultiplied_alpha = 0;
	*/
	v.caps = STMFBIO_VAR_CAPS_OPACITY | STMFBIO_VAR_CAPS_PREMULTIPLIED;
	v.opacity = 0xff - (level * 0xff / 100);
	if (ioctl(fd, STMFBIO_SET_VAR_SCREENINFO_EX, &v) < 0)
		perror(LOGTAG "setBlendLevel STMFBIO");
}

#if 0
/* this is not accelerated... */
void CFbAccelSTi::paintPixel(const int x, const int y, const fb_pixel_t col)
{
	fb_pixel_t *pos = getFrameBufferPointer();
	pos += (stride / sizeof(fb_pixel_t)) * y;
	pos += x;
	*pos = col;
}

/* unused, because horizontal and vertical line are not acceleratedn in paintRect anyway
 * and everything else is identical to fb_generic code */
void CFbAccelSTi::paintLine(int xa, int ya, int xb, int yb, const fb_pixel_t col)
{
	int dx = abs (xa - xb);
	int dy = abs (ya - yb);
	if (dy == 0) /* horizontal line */
	{
		/* paintRect actually is 1 pixel short to the right,
		 * but that's bug-compatibility with the GXA code */
		paintRect(xa, ya, xb - xa, 1, col);
		return;
	}
	if (dx == 0) /* vertical line */
	{
		paintRect(xa, ya, 1, yb - ya, col);
		return;
	}
	int x;
	int y;
	int End;
	int step;

	if (dx > dy)
	{
		int p = 2 * dy - dx;
		int twoDy = 2 * dy;
		int twoDyDx = 2 * (dy-dx);

		if (xa > xb)
		{
			x = xb;
			y = yb;
			End = xa;
			step = ya < yb ? -1 : 1;
		}
		else
		{
			x = xa;
			y = ya;
			End = xb;
			step = yb < ya ? -1 : 1;
		}

		paintPixel(x, y, col);

		while (x < End)
		{
			x++;
			if (p < 0)
				p += twoDy;
			else
			{
				y += step;
				p += twoDyDx;
			}
			paintPixel(x, y, col);
		}
	}
	else
	{
		int p = 2 * dx - dy;
		int twoDx = 2 * dx;
		int twoDxDy = 2 * (dx-dy);

		if (ya > yb)
		{
			x = xb;
			y = yb;
			End = ya;
			step = xa < xb ? -1 : 1;
		}
		else
		{
			x = xa;
			y = ya;
			End = yb;
			step = xb < xa ? -1 : 1;
		}

		paintPixel(x, y, col);

		while (y < End)
		{
			y++;
			if (p < 0)
				p += twoDx;
			else
			{
				x += step;
				p += twoDxDy;
			}
			paintPixel(x, y, col);
		}
	}
	mark(xa, ya, xb, yb);
	blit();
}
#endif
