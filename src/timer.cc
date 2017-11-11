/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/timer>

namespace tscb {

std::chrono::steady_clock::time_point monotonic_time(void) noexcept
{
	return std::chrono::steady_clock::now();
}

}
