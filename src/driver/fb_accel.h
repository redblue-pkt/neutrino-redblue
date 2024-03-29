/*
	Copyright (C) 2007-2013,2017-2018 Stefan Seyfried

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

	private functions for the fbaccel class (only used in CFrameBuffer)
*/


#ifndef __fbaccel__
#define __fbaccel__
#include <config.h>
#include <OpenThreads/Mutex>
#include <OpenThreads/ScopedLock>
#include <OpenThreads/Thread>
#include <OpenThreads/Condition>
#include "fb_generic.h"

class CFbAccel
	: public CFrameBuffer
{
	public:
		CFbAccel();
		~CFbAccel();
		void paintBoxRel(const int x, const int y, const int dx, const int dy, const fb_pixel_t col, int radius, int type);
		virtual void paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col);
};

class CFbAccelSTi
	: public OpenThreads::Thread, public CFbAccel
{
	private:
		void run(void);
		void blit(void);
		void _blit(void);
		bool blit_thread;
		bool blit_pending;
		OpenThreads::Condition blit_cond;
		OpenThreads::Mutex blit_mutex;
		fb_pixel_t *backbuffer;
	public:
		CFbAccelSTi();
		~CFbAccelSTi();
		void init(const char * const);
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		void paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col);
		void blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp, uint32_t yp, bool transp);
		void waitForIdle(const char *func = NULL);
		void mark(int x, int y, int dx, int dy);
		fb_pixel_t * getBackBufferPointer() const;
		void setBlendMode(uint8_t);
		void setBlendLevel(int);
};

class CFbAccelCSHDx
	: public CFbAccel
{
	private:

	protected:
		OpenThreads::Mutex mutex;

		int fbCopy(uint32_t *mem_p, int width, int height, int dst_x, int dst_y, int src_x, int src_y, int mode);
		int fbFill(int sx, int sy, int width, int height, fb_pixel_t color, int mode=0);

	public:
		CFbAccelCSHDx();
//		~CFbAccelCSHDx();

#if 0
		/* TODO: Run this functions with hardware acceleration */
		void SaveScreen(int x, int y, int dx, int dy, fb_pixel_t * const memp);
		void RestoreScreen(int x, int y, int dx, int dy, fb_pixel_t * const memp);
		void Clear();
#endif
};

class CFbAccelCSHD1
	: public CFbAccelCSHDx
{
	private:
		fb_pixel_t lastcol;
		fb_pixel_t *backbuffer;
		int		  devmem_fd;	/* to access the GXA register we use /dev/mem */
		unsigned int	  smem_start;	/* as aquired from the fbdev, the framebuffers physical start address */
		volatile uint8_t *gxa_base;	/* base address for the GXA's register access */

		void setColor(fb_pixel_t col);

	public:
		CFbAccelCSHD1();
		~CFbAccelCSHD1();
		void init(const char * const);
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		void paintPixel(int x, int y, const fb_pixel_t col);
		void paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col);
		void paintLine(int xa, int ya, int xb, int yb, const fb_pixel_t col);
		inline void paintHLineRel(int x, int dx, int y, const fb_pixel_t col) { paintLine(x, y, x+dx, y, col); };
		inline void paintVLineRel(int x, int y, int dy, const fb_pixel_t col) { paintLine(x, y, x, y+dy, col); };
		void paintBoxRel(const int x, const int y, const int dx, const int dy, const fb_pixel_t col, int radius = 0, int type = CORNER_ALL);
		void fbCopyArea(uint32_t width, uint32_t height, uint32_t dst_x, uint32_t dst_y, uint32_t src_x, uint32_t src_y);
		void blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp = 0, uint32_t yp = 0, bool transp = false, uint32_t unscaled_w = 0, uint32_t unscaled_h = 0); //NI
		void blitBox2FB(const fb_pixel_t* boxBuf, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff);
		void waitForIdle(const char *func = NULL);
		fb_pixel_t * getBackBufferPointer() const;
		void setBlendMode(uint8_t);
		void setBlendLevel(int);
		void add_gxa_sync_marker(void);
		void setupGXA(void);
		void setOsdResolutions();
};

class CFbAccelCSHD2
	: public CFbAccelCSHDx
{
	private:
		fb_pixel_t *backbuffer;
		int sysRev;
		bool IsApollo;

	public:
		CFbAccelCSHD2();
//		~CFbAccelCSHD2();
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		void paintHLineRel(int x, int dx, int y, const fb_pixel_t col);
		void paintVLineRel(int x, int y, int dy, const fb_pixel_t col);
		void paintBoxRel(const int x, const int y, const int dx, const int dy, const fb_pixel_t col, int radius = 0, int type = CORNER_ALL);
		void fbCopyArea(uint32_t width, uint32_t height, uint32_t dst_x, uint32_t dst_y, uint32_t src_x, uint32_t src_y);
		void blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp = 0, uint32_t yp = 0, bool transp = false, uint32_t unscaled_w = 0, uint32_t unscaled_h = 0); //NI
		void blitBox2FB(const fb_pixel_t* boxBuf, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff);
		fb_pixel_t * getBackBufferPointer() const;
		void setBlendMode(uint8_t);
		void setBlendLevel(int);
		int scale2Res(int size);
		bool fullHdAvailable();
		void setOsdResolutions();
		uint32_t getWidth4FB_HW_ACC(const uint32_t x, const uint32_t w, const bool max=true);
		bool needAlign4Blit() { return true; };
};

class CFbAccelGLFB
	: public OpenThreads::Thread, public CFbAccel
{
	private:
		void run(void);
		void blit(void);
		void _blit(void);
		bool blit_thread;
		bool blit_pending;
		OpenThreads::Condition blit_cond;
		OpenThreads::Mutex blit_mutex;
		fb_pixel_t *backbuffer;
	public:
		CFbAccelGLFB();
		~CFbAccelGLFB();
		void init(const char * const);
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		void blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp, uint32_t yp, bool transp = false, uint32_t unscaled_w = 0, uint32_t unscaled_h = 0); //NI
		fb_pixel_t * getBackBufferPointer() const;
};

class CFbAccelARM
#if ENABLE_ARM_ACC
	: public OpenThreads::Thread, public CFbAccel
#else
	: public CFbAccel
#endif

{
	private:
#if ENABLE_ARM_ACC
		void run(void);
		void blit(void);
		void _blit(void);
		bool blit_thread;
		bool blit_pending;
		OpenThreads::Condition blit_cond;
		OpenThreads::Mutex blit_mutex;
#endif
		fb_pixel_t *backbuffer;
	public:
		CFbAccelARM();
		~CFbAccelARM();
		fb_pixel_t * getBackBufferPointer() const;
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		int scale2Res(int size);
		bool fullHdAvailable();
		void setOsdResolutions();
		void setBlendMode(uint8_t mode);
		void setBlendLevel(int level);
#if ENABLE_ARM_ACC
		void paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col);
#endif
};

class CFbAccelMIPS
#if ENABLE_MIPS_ACC
	: public OpenThreads::Thread, public CFbAccel
#else
	: public CFbAccel
#endif
{
	private:
#if ENABLE_MIPS_ACC
		void run(void);
		void blit(void);
		void _blit(void);
		bool blit_thread;
		bool blit_pending;
		OpenThreads::Condition blit_cond;
		OpenThreads::Mutex blit_mutex;
#endif
		fb_pixel_t *backbuffer;
	public:
		CFbAccelMIPS();
		~CFbAccelMIPS();
		fb_pixel_t * getBackBufferPointer() const;
		int setMode(unsigned int xRes, unsigned int yRes, unsigned int bpp);
		int scale2Res(int size);
		bool fullHdAvailable();
		void setOsdResolutions();
		void setBlendMode(uint8_t mode);
		void setBlendLevel(int level);
#if ENABLE_MIPS_ACC
		void paintRect(const int x, const int y, const int dx, const int dy, const fb_pixel_t col);
#endif
};

#endif
