// framework.h : include file for standard system include files

#pragma once

#include "targetver.h"

// Windows Header Files
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <taskschd.h>
#include <comdef.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// Library links
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")