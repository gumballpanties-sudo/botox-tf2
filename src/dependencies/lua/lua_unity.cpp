/* lua 5.4.7 as one c++ tu: lua_error throws c++ exceptions, so destructors in bindings run */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define MAKE_LIB
#include "onelua.c"
