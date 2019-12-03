/*
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <stdio.h>
#include <stdlib.h>

#define panic(...) \
do {\
        fprintf(stderr, __VA_ARGS__);\
        abort();\
} while(0)

#define ASSERT(x) \
do {\
        if (!(x)) panic("assertion '%s' failed at %s:%d\n", #x, __FILE__, __LINE__);\
} while(0)

#define EXPECTED(x) \
panic("Expected %s at %s:%d\n", x, __FILE__, __LINE__)

#define UNEXPECTED(x) \
panic("Unexpected %s at %s:%d\n", x, __FILE__, __LINE__)
