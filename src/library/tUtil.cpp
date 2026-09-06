// tUtil.cpp: implementation of the tUtil class.
//
//////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <process.h>  // spawnlp
#include <limits.h>
#include <math.h>

#include "tUtil.h"
#include "tLuaCOMException.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

#ifndef _MSC_VER  // not MSVC++
# define _spawnlp spawnlp
#endif

#define MAX_VALID_STRING_SIZE 1000

namespace
{
bool isLeapYear(WORD year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

WORD daysInMonth(WORD year, WORD month)
{
  static const WORD days[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if(month < 1 || month > 12)
    return 0;
  if(month == 2 && isLeapYear(year))
    return 29;
  return days[month - 1];
}

BOOL decrementSystemTimeOneSecond(SYSTEMTIME* system_time)
{
  SYSTEMTIME adjusted = *system_time;
  if(adjusted.wSecond > 0)
    --adjusted.wSecond;
  else
  {
    adjusted.wSecond = 59;
    if(adjusted.wMinute > 0)
      --adjusted.wMinute;
    else
    {
      adjusted.wMinute = 59;
      if(adjusted.wHour > 0)
        --adjusted.wHour;
      else
      {
        adjusted.wHour = 23;
        if(adjusted.wDay > 1)
          --adjusted.wDay;
        else
        {
          if(adjusted.wMonth > 1)
            --adjusted.wMonth;
          else
          {
            if(adjusted.wYear <= 100)
              return FALSE;
            --adjusted.wYear;
            adjusted.wMonth = 12;
          }
          adjusted.wDay = daysInMonth(adjusted.wYear, adjusted.wMonth);
        }
        if(adjusted.wDayOfWeek <= 6)
          adjusted.wDayOfWeek = (adjusted.wDayOfWeek + 6) % 7;
      }
    }
  }

  *system_time = adjusted;
  return TRUE;
}

BOOL incrementSystemTimeOneSecond(SYSTEMTIME* system_time)
{
  SYSTEMTIME adjusted = *system_time;
  if(adjusted.wSecond < 59)
    ++adjusted.wSecond;
  else
  {
    adjusted.wSecond = 0;
    if(adjusted.wMinute < 59)
      ++adjusted.wMinute;
    else
    {
      adjusted.wMinute = 0;
      if(adjusted.wHour < 23)
        ++adjusted.wHour;
      else
      {
        adjusted.wHour = 0;
        const WORD month_days = daysInMonth(adjusted.wYear, adjusted.wMonth);
        if(month_days == 0)
          return FALSE;
        if(adjusted.wDay < month_days)
          ++adjusted.wDay;
        else
        {
          adjusted.wDay = 1;
          if(adjusted.wMonth < 12)
            ++adjusted.wMonth;
          else
          {
            if(adjusted.wYear >= 9999)
              return FALSE;
            ++adjusted.wYear;
            adjusted.wMonth = 1;
          }
        }
        if(adjusted.wDayOfWeek <= 6)
          adjusted.wDayOfWeek = (adjusted.wDayOfWeek + 1) % 7;
      }
    }
  }

  *system_time = adjusted;
  return TRUE;
}
}

// https://github.com/windtail/luacom/commit/3852e47c2f0fe77477cbde54541228cd2f7b8901
// - in none-english environment (i am Chinese) we DO NOT use CP_UTF8, CP_ACP should be used
// - while Cygwin default convert filename internally to UTF-8
// - we have to use ASCII format for our lua source code, if you prefer UTF-8, you need luaiconv to convert UTF-8 to your ASCII format (GBK or other)
#ifdef __CYGWIN__
LUACOM_THREAD_LOCAL UINT code_page=CP_UTF8; // By default, Cygwin internally convert filename to UTF-8
#else
LUACOM_THREAD_LOCAL UINT code_page=CP_ACP;
#endif

FILE* tUtil::log_file = NULL;
CRITICAL_SECTION log_file_cs;
volatile bool g_log_file_cs_initialized = false;
// log methods are all static; there's no clear initialization time;
// so just always check that the CS is initialized before using it
void CSInit()
{
  if(!g_log_file_cs_initialized)
  {
	  g_log_file_cs_initialized = true;
	  InitializeCriticalSection(&log_file_cs);
  }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

bool tUtil::IsValidString(LPCTSTR string)
{
  bool return_value = string != NULL;

  assert(return_value);

  return return_value;
}

tStringBuffer tUtil::GetErrorMessage(DWORD errorcode)
{
  LPSTR lpMsgBuf;
  DWORD result = 0;

  result = FormatMessageA( 
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL,
    errorcode,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
    (LPSTR)&lpMsgBuf,
    0,
    NULL);
  // note: non-Unicode (FormatMessageA not FormatMessage/LPTSTR).  How would we
  // propogate Unicode error messages to Lua?

  if(result == 0)
    return NULL;

  // Strip the newline at the end of the message

  while((result > 1) && ((lpMsgBuf[result-1] == '\n') || (lpMsgBuf[result-1] == '\r')))
    result--;
  lpMsgBuf[result] = '\0';

  tStringBuffer ret(lpMsgBuf);

  // Free the buffer.
  LocalFree( lpMsgBuf );

  return ret;
}


tStringBuffer tUtil::bstr2string(BSTR bstr, bool nullTerminated)
{
  if(bstr == NULL) // NULL BSTR indicates empty string.
    return "";

  UINT lenWide = SysStringLen(bstr); // not including '\0' terminator
  if(lenWide > INT_MAX)
    LUACOM_ERROR("string too long");

  if(lenWide == 0)
    return "";

  // gets string length
  int lenMulti = WideCharToMultiByte(
    code_page, 0, bstr, static_cast<int>(lenWide), NULL, 0, NULL, NULL);

  if(!lenMulti)
    LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));

  struct C
  {
    C(int size) { s = new char[size]; }
    ~C() { delete [] s; }
    char * s;
  } str(lenMulti + (nullTerminated ? 1 : 0));

  int result = WideCharToMultiByte(
    code_page, 0, bstr, static_cast<int>(lenWide), str.s, lenMulti,
    NULL, NULL);

  if(!result)
    LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));
  if(result != lenMulti)
    LUACOM_ERROR("string conversion was incomplete");

  if(nullTerminated)
    str.s[lenMulti] = '\0';
  return tStringBuffer(str.s, lenMulti + (nullTerminated ? 1 : 0));
}

BSTR tUtil::string2bstr(const char * string, size_t len)
{
  if(!string)
    return NULL;

  if(len == 0)
  {
    BSTR bstr = SysAllocStringLen(NULL, 0);
    CHKMALLOC(bstr);
    return bstr;
  }

  const bool nullTerminated = len == static_cast<size_t>(-1);
  if(!nullTerminated && len > INT_MAX)
    LUACOM_ERROR("string too long");

  const int sourceLength = nullTerminated ? -1 : static_cast<int>(len);
  const int lenWide =
    MultiByteToWideChar(code_page, 0, string, sourceLength, NULL, 0);
  if(lenWide == 0)
    LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));

  const UINT bstrLength = static_cast<UINT>(
    nullTerminated ? lenWide - 1 : lenWide);
  BSTR bstr = SysAllocStringLen(NULL, bstrLength);
  CHKMALLOC(bstr);

  const int converted =
    MultiByteToWideChar(code_page, 0, string, sourceLength, bstr, lenWide);
  if(!converted)
  {
    const DWORD error = GetLastError();
    SysFreeString(bstr);
    LUACOM_ERROR(tUtil::GetErrorMessage(error));
  }
  if(converted != lenWide)
  {
    SysFreeString(bstr);
    LUACOM_ERROR("string conversion was incomplete");
  }

  return bstr;
}

bool tUtil::OpenLogFile(const char *name)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  tUtil::CloseLogFile();

  tUtil::log_file = fopen(name, "w");

  if(!tUtil::log_file)
    return false;
  else
    return true;
}

void tUtil::CloseLogFile()
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  if(tUtil::log_file)
  {
    fclose(tUtil::log_file);
    tUtil::log_file = NULL;
  }
}

void tUtil::log(const char *who, const char *what, ...)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  if(tUtil::log_file && who && what)
  {
    int size = 0;

    fprintf(tUtil::log_file, "%s:", who);

    va_list marker;
    va_start(marker, what);

    size = vfprintf(tUtil::log_file, what, marker);

    va_end(marker);

    if(what[strlen(what) - 1] != '\n')
      fprintf(tUtil::log_file, "\n");

    fflush(tUtil::log_file);

#ifdef VERBOSE

    char *buffer = new char[size+1];

    sprintf(buffer, "%s:", who);

    va_start(marker, what);

    size = vsprintf(buffer, what, marker);

    va_end(marker);

    MessageBoxA(NULL, buffer, "LuaCOM Log", MB_OK | MB_ICONEXCLAMATION);

    delete[] buffer;
    buffer = NULL;

#endif // VERBOSE
  }
}

void tUtil::log_verbose(const char *who, const char *what, ...)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
#ifdef VERBOSE
  if(tUtil::log_file && who && what)
  {
    fprintf(tUtil::log_file, "%s:", who);

    va_list marker;
    va_start(marker, what);

    vfprintf(tUtil::log_file, what, marker);

    va_end(marker);

    if(what[strlen(what) - 1] != '\n')
      fprintf(tUtil::log_file, "\n");

    fflush(tUtil::log_file);
  }
#endif
}


char * tUtil::strdup(const char *string)
{
  if(!string)
    return NULL;

  char *new_string = (char *) malloc(strlen(string)+1);

  strcpy(new_string, string);

  return new_string;
}

void tUtil::ShowHelp(const char *filename, unsigned long context)
{
  // filename must have at least the extension
  if(!filename || strlen(filename) < 5)
    return;

  const char* extension = &filename[strlen(filename) - 4];

  if(_stricmp(extension, ".chm") == 0)
  {
    char context_param[50];
  
    if(context != 0)
      sprintf(context_param, "-mapid %lu", context);
    else
      context_param[0] = '\0';
    _spawnlp(_P_NOWAIT, "hh.exe", "hh.exe", context_param, filename, NULL);
  }
  else if(_stricmp(extension, ".hlp") == 0)
  {
    if(context != 0)
      WinHelpA(NULL, filename, HELP_CONTEXT, context);
    else
      WinHelpA(NULL, filename, HELP_FINDER, 0);
  }
}

void tUtil::RegistrySetString(lua_State* L, const char& Key, const char* value)
{
	lua_pushlightuserdata(L, (void *)&Key);  /* push address */ 
	lua_pushstring(L, value); 
	/* registry[&Key] = value */ 
    lua_settable(L, LUA_REGISTRYINDEX); 
}

tStringBuffer tUtil::RegistryGetString(lua_State* L, const char& Key)
{
	lua_pushlightuserdata(L, (void *)&Key);  /* push address */ 
    lua_gettable(L, LUA_REGISTRYINDEX);  /* retrieve value */ 
    return tStringBuffer(lua_tostring(L, -1));  /* convert to string */
}

BOOL tUtil::VariantTimeToSystemTimeWithMilliseconds(
  double variant_time,
  SYSTEMTIME* system_time)
{
  if(system_time == NULL)
    return FALSE;

  if(!VariantTimeToSystemTime(variant_time, system_time))
    return FALSE;

  double whole_days = 0.0;
  double fraction = modf(variant_time, &whole_days);
  if(fraction < 0.0)
    fraction = -fraction;

  const DWORD seconds_per_day = 24 * 60 * 60;
  const double milliseconds_per_day = seconds_per_day * 1000.0;
  double total_milliseconds = floor(
    fraction * milliseconds_per_day + 0.5);
  if(total_milliseconds >= milliseconds_per_day)
    total_milliseconds = 0.0;

  const DWORD rounded_milliseconds =
    static_cast<DWORD>(total_milliseconds);
  const DWORD target_second = rounded_milliseconds / 1000;
  const DWORD converted_second =
    system_time->wHour * 60 * 60
    + system_time->wMinute * 60
    + system_time->wSecond;

  if(converted_second == (target_second + 1) % seconds_per_day)
  {
    if(!decrementSystemTimeOneSecond(system_time))
      return FALSE;
  }
  else if(converted_second != target_second)
    return FALSE;

  system_time->wMilliseconds = static_cast<WORD>(
    rounded_milliseconds % 1000);
  return TRUE;
}

BOOL tUtil::SystemTimeToVariantTimeWithMilliseconds(
  SYSTEMTIME system_time,
  double* variant_time)
{
  if(variant_time == NULL)
    return FALSE;

  const WORD milliseconds = system_time.wMilliseconds;
  system_time.wMilliseconds = 0;

  double whole_seconds = 0.0;
  if(!SystemTimeToVariantTime(&system_time, &whole_seconds))
    return FALSE;

  const double fraction = milliseconds / (24.0 * 60.0 * 60.0 * 1000.0);
  *variant_time = whole_seconds < 0.0
    ? whole_seconds - fraction
    : whole_seconds + fraction;
  return TRUE;
}

BOOL tUtil::RoundSystemTimeToNearestSecond(SYSTEMTIME* system_time)
{
  if(system_time == NULL)
    return FALSE;

  if(system_time->wMilliseconds < 500)
  {
    system_time->wMilliseconds = 0;
    return TRUE;
  }

  if(!incrementSystemTimeOneSecond(system_time))
  {
    SetLastError(ERROR_INVALID_TIME);
    return FALSE;
  }
  system_time->wMilliseconds = 0;
  return TRUE;
}
