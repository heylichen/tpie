// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; c-file-style: "stroustrup"; -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2008, 2012, The TPIE development team
// 
// This file is part of TPIE.
// 
// TPIE is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// TPIE is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with TPIE.  If not, see <http://www.gnu.org/licenses/>

#ifndef _TPIE_CPU_TIMER_H
#define _TPIE_CPU_TIMER_H

///////////////////////////////////////////////////////////////////////////
/// \file cpu_timer.h
/// Timer measuring user time, system time and wall clock time.
///////////////////////////////////////////////////////////////////////////

#include <tpie/tpie_export.h>
// Get definitions for working with Unix and Windows
#include <tpie/portability.h>
#include <iostream>
#include <time.h>
#ifndef _WIN32
#include <sys/times.h>
#include <sys/resource.h>
#endif

namespace tpie {

///////////////////////////////////////////////////////////////////////////
/// A timer measuring user time, system time and wall clock time.  The
/// timer can be start()'ed, stop()'ed, and queried. Querying can be
/// done without stopping the timer, to report intermediate values.
/// \internal
///////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
typedef int tms;
#else
using ::tms;
#endif
class TPIE_EXPORT cpu_timer {

private:
	long        clock_tick_;

#ifndef _WIN32
	tms last_sync_;
	tms elapsed_;
#endif

	clock_t     last_sync_real_;
	clock_t     elapsed_real_;

	bool        running_;

	inline void set_clock_tick();
	inline void last_sync_real_declaration();

public:
	cpu_timer();

	///////////////////////////////////////////////////////////////////////////
	/// Start the timer.
	///////////////////////////////////////////////////////////////////////////
	void start();

	///////////////////////////////////////////////////////////////////////////
	/// Stop the timer.
	///////////////////////////////////////////////////////////////////////////
	void stop();

	///////////////////////////////////////////////////////////////////////////
	/// Update fields such that running(), clock_tick(), elapsed(),
	/// elapsed_real() will return recent measurements.
	///////////////////////////////////////////////////////////////////////////
	void sync();

	///////////////////////////////////////////////////////////////////////////
	/// Reset the timer to zero.
	///////////////////////////////////////////////////////////////////////////
	void reset();

	///////////////////////////////////////////////////////////////////////////
	/// Linux: Query the amount of time spent by this process in user mode
	/// since the timer was reset.
	///
	/// Windows: Query the amount of wall clock time spent by this process
	/// since the timer was reset.
	///////////////////////////////////////////////////////////////////////////
	double user_time();

	///////////////////////////////////////////////////////////////////////////
	/// Linux: Query the amount of time spent by this process in kernel mode
	/// since the timer was reset.
	///
	/// Windows: Query the amount of wall clock time spent by this process
	/// since the timer was reset.
	///////////////////////////////////////////////////////////////////////////
	double system_time();

	///////////////////////////////////////////////////////////////////////////
	/// Query the amount of wall clock time spent by this process since the
	/// timer was reset.
	///////////////////////////////////////////////////////////////////////////
	double wall_time();

	///////////////////////////////////////////////////////////////////////////
	/// Tell whether the timer is currently running.
	///////////////////////////////////////////////////////////////////////////
	inline bool running() const {
		return running_;
	}

	///////////////////////////////////////////////////////////////////////////
	/// Return number of ticks per wall clock second as reported by the OS.
	///////////////////////////////////////////////////////////////////////////
	inline long clock_tick() const {
		return clock_tick_;
	}

	///////////////////////////////////////////////////////////////////////////
	/// Return the timestamp of last sync.
	///////////////////////////////////////////////////////////////////////////
	inline tms last_sync() const {
		#ifdef _WIN32
		return 0;
		#else
		return last_sync_;
		#endif
	}

	///////////////////////////////////////////////////////////////////////////
	/// Return the timestamp indicating the elapsed time. Only useful on Linux.
	///////////////////////////////////////////////////////////////////////////
	inline tms elapsed() const {
		#ifdef _WIN32
		return 0;
		#else
		return elapsed_;
		#endif
	}

	///////////////////////////////////////////////////////////////////////////
	/// Return the wall clock timestamp of the last sync.
	///////////////////////////////////////////////////////////////////////////
	inline clock_t last_sync_real() const {
		return last_sync_real_;
	}

	///////////////////////////////////////////////////////////////////////////
	/// Return the elapsed wall clock time at the last sync.
	///////////////////////////////////////////////////////////////////////////
	inline clock_t elapsed_real() const {
		return elapsed_real_;
	}

};

///////////////////////////////////////////////////////////////////////////////
/// Enable outputting the queriable values of this timer. On Windows, just
/// output the elapsed real time in seconds. On Linux, output user, system and
/// wall clock time in seconds.
///////////////////////////////////////////////////////////////////////////////
TPIE_EXPORT std::ostream &operator<<(std::ostream &s, cpu_timer &ct);

}

#endif // _TPIE_CPU_TIMER_H
