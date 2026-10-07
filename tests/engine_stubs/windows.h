#pragma once

// Portable harness only: these substitutions do not model Windows SEH/ABI.
#include <cstdarg>
#include <strings.h>
#define __try try
#define __except(...) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define _strnicmp strncasecmp
