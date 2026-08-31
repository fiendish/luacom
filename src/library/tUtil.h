// tUtil.h: interface for the tUtil class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TUTIL_H__0B3E0B68_981F_11D3_A906_0004ACE655F9__INCLUDED_)
#define AFX_TUTIL_H__0B3E0B68_981F_11D3_A906_0004ACE655F9__INCLUDED_

#include <windows.h>
#include <stdio.h>
#include "tStringBuffer.h"  // Added by ClassView

#if defined(_MSC_VER)
#define LUACOM_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__)
#define LUACOM_THREAD_LOCAL __thread
#else
#define LUACOM_THREAD_LOCAL thread_local
#endif

extern LUACOM_THREAD_LOCAL UINT code_page;		// can be set by Lua
struct lua_State;
class tUtil  
{
public:
  static void ShowHelp(const char* filename, unsigned long context);
  static char * strdup(const char *string);
  static void log(const char *who, const char *what,...);
  static void log_verbose(const char *who, const char *what,...);
  static void CloseLogFile(void);
  static bool OpenLogFile(const char *name);
  static BSTR string2bstr(const char *string, size_t len = -1);
  static tStringBuffer bstr2string(BSTR bstr, bool nullTerminated = true);
  static tStringBuffer GetErrorMessage(DWORD errorcode);
  static bool IsValidString(LPCTSTR string);
  static void RegistrySetString(lua_State* L, const char& Key, const char* value);
  static tStringBuffer RegistryGetString(lua_State* L, const char& Key);
  static BOOL VariantTimeToSystemTimeWithMilliseconds(double variant_time, SYSTEMTIME* system_time);
  static BOOL SystemTimeToVariantTimeWithMilliseconds(SYSTEMTIME system_time, double* variant_time);
  static BOOL RoundSystemTimeToNearestSecond(SYSTEMTIME* system_time);

  static FILE* log_file;
};

class CriticalSectionObject
{
protected:
	LPCRITICAL_SECTION m_cs;
public:
	CriticalSectionObject(LPCRITICAL_SECTION cs)
	{
		m_cs = cs;
		EnterCriticalSection(m_cs);
	}
	~CriticalSectionObject()
	{
		LeaveCriticalSection(m_cs);
	}
};

// algumas macros uteis

#define UNUSED(x) (void)(x)

#define SAFEFREE(x) {if(x){free(x); (x) = NULL;}}

#define SAFEDELETE(x) {if(x){delete x; (x) = NULL;}}

#define SAFEDELETEARR(x) {if(x){delete[] x; (x) = NULL;}}

#endif // !defined(AFX_TUTIL_H__0B3E0B68_981F_11D3_A906_0004ACE655F9__INCLUDED_)
