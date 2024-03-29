/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2012-2013 defans@bluepeercrew.us

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

#ifndef __CSCREENSAVER_H__
#define __CSCREENSAVER_H__

#include <vector>
#include <string>
#include "gui/components/cc.h"
#include <mutex>
#include <thread>


class CFrameBuffer;
class CPictureViewer;
class CScreenSaver : public sigc::trackable
{
	private:
		CFrameBuffer 	*m_frameBuffer;
		CPictureViewer	*m_viewer;

		std::thread	*thrScreenSaver;
		static void	ScreenSaverPrg(CScreenSaver *scr);
		bool thr_exit;
		std::mutex	scr_mutex;

		std::vector<std::string> v_bg_files;
		unsigned int 	index;
		t_channel_id	pip_channel_id[3];
		bool		force_refresh;
		bool		status_mute;
		uint 		seed[6];

		void		handleRadioText(bool enable_paint);
		void		hideRadioText();

		bool ReadDir();
		void paint();

		time_t idletime;

		union u_color
		{
			struct s_color
			{
				uint8_t b, g, r, a;
			} uc_color;
			unsigned int i_color;
		};

		u_color clr;
		void thrExit();
		sigc::slot<void> sl_scr_stop;

	public:
		typedef enum
		{
			SCR_MODE_IMAGE,
			SCR_MODE_CLOCK,
			SCR_MODE_CLOCK_COLOR
		} SCR_MODE_T;

		typedef enum
		{
			SCR_MODE_TEXT_OFF,
			SCR_MODE_TEXT_ON

		} SCR_MODE_TEXT_T;

		CScreenSaver();
		~CScreenSaver();
		static CScreenSaver *getInstance();
		bool canStart();
		bool isActive();
		void Start();
		void Stop();
		bool ignoredMsg(neutrino_msg_t msg);
		sigc::signal<void> OnBeforeStart;
		sigc::signal<void> OnAfterStart;
		sigc::signal<void> OnAfterStop;

		void resetIdleTime() { idletime = time(NULL); }
		time_t getIdleTime() { return idletime; }
		void forceRefresh() { force_refresh = true; }
		static CComponentsFrmClock *getClockObject();
};

#endif // __CSCREENSAVER_H__
