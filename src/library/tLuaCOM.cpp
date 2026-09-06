/*
 * tLuaCOM.cpp
 *
 *  Implementacao da classe tLuaCOM
 *
 * Renato Cerqueira
 * Vinicius Almendra
 */

#include "tLuaCOM.h"
#include "tLuaDispatch.h"
#include "LuaAux.h"
#include "tUtil.h"
#include "tLuaCOMException.h"
#include "tCOMUtil.h"
#include "LuaCompat.h"
#include <exception>

#if defined(__CYGWIN__) || defined(__MINGW32__)
#include <initguid.h>
DEFINE_GUID(IID_IProxyManager, 0x00000008, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#endif

volatile long tLuaCOM::NEXT_ID = 0;

namespace
{
class ExceptionInfoStrings
{
public:
  explicit ExceptionInfoStrings(EXCEPINFO& exception_info)
    : info(exception_info) {}

  ~ExceptionInfoStrings()
  {
    SysFreeString(info.bstrSource);
    SysFreeString(info.bstrDescription);
    SysFreeString(info.bstrHelpFile);
  }

private:
  EXCEPINFO& info;
};
}

// Keep call resources outside the protected Lua frame. Lua errors can skip
// C++ destructors when the Lua runtime uses longjmp.
struct tLuaCOM::CallContext
{
  CallContext(tLuaCOM* object, DISPID dispid, int invkind,
              FUNCDESC* funcdesc, tLuaObjList params)
    : object(object), dispid(dispid), invkind(invkind), funcdesc(funcdesc),
      params(params), exception_strings(excepinfo)
  {
    ZeroMemory(&dispparams, sizeof(dispparams));
    VariantInit(&result);
    ZeroMemory(&excepinfo, sizeof(excepinfo));
  }

  ~CallContext()
  {
    object->typehandler->releaseVariants(&dispparams);
    VariantClear(&result);
  }

  tLuaCOM* object;
  DISPID dispid;
  int invkind;
  FUNCDESC* funcdesc;
  tLuaObjList params;
  tCOMPtr<IDispatch> active_dispatch;
  DISPPARAMS dispparams;
  VARIANTARG result;
  EXCEPINFO excepinfo;
  ExceptionInfoStrings exception_strings;
  std::exception_ptr exception;
};

tLuaCOM::tLuaCOM(lua_State* L,
                 IDispatch *pdisp_arg,
                 ITypeInfo *ptinfo_arg,
                 CLSID coclass
                 )
{
  HRESULT hr = S_OK;
	
  // initialization
  clsid                   = coclass;
  lock_count              = 0;
  objName                 = NULL;
  last_connection_interface = IID_NULL;
  last_connection_cookie  = 0;

  pdisp.Attach(pdisp_arg);
  pdisp->AddRef(); 

  if(ptinfo_arg)
  {
    // gets ITypeComp interface. If it's not
    // available, frees typeinfo, as we only
    // use ITypeComp

    hr = ptinfo_arg->GetTypeComp(&ptcomp);

    if(SUCCEEDED(hr))
    {
      ptinfo.Attach(ptinfo_arg);
      ptinfo->AddRef();

      // tries to get typecomp for type library
      // (useful to bind to constants)

      tCOMPtr<ITypeLib> ptlib;
      unsigned int dumb = 0;
      hr = ptinfo->GetContainingTypeLib(&ptlib, &dumb);
      if(SUCCEEDED(hr))
      {
        //ptlib->GetTypeComp(&plib_tcomp);
        plib_tcomp.Release();
      }
    }
  }

  typehandler = new tLuaCOMTypeHandler(ptinfo_arg);

  ID = InterlockedIncrement(&(tLuaCOM::NEXT_ID));

#ifdef VERBOSE
  {
    char msg[100];
    lua_Debug ar;

    if(lua_getstack(L, 1, &ar))
    {
      lua_getinfo(L, "lnS", &ar);

      sprintf(msg, "%.4d:created:LUAINFO=(%s,%d,%s,%s)", ID, ar.short_src,
        ar.currentline, ar.name, ar.what);
    }
    else if(lua_getstack(L, 0, &ar))
    {
      lua_getinfo(L, "lnS", &ar);
      sprintf(msg, "%.4d:created:LUAINFO=(%s,%d,%s,%s)", ID, ar.short_src,
        ar.currentline, ar.name, ar.what);
    }
    else
    {
      sprintf(msg, "%.4d:created:LUAINFO=(not available)", ID);
    }

    tUtil::log_verbose("tLuaCOM",msg);
  }
#endif
}

tLuaCOM::~tLuaCOM()
{
  releaseConnections();

  // frees funcinfos
  {
    for(size_t counter = 0; counter < mFuncInfo.size(); counter++)
    {
      free(mFuncInfo[counter].name);
      mFuncInfo[counter].name = NULL;

      ReleaseFuncDesc(mFuncInfo[counter].propget_owner,
                      mFuncInfo[counter].propget);
      ReleaseFuncDesc(mFuncInfo[counter].propput_owner,
                      mFuncInfo[counter].propput);
      ReleaseFuncDesc(mFuncInfo[counter].func_owner,
                      mFuncInfo[counter].func);
    }

  }

  if (objName) {
    free(objName);
    objName = NULL;
  }

  if( typehandler) {
	delete typehandler;
    typehandler = NULL;
  }

  tUtil::log_verbose("tLuaCOM", "%.4d:destroyed", ID);
}

static void ReleaseBindResult(ITypeInfo*& info,
                              DESCKIND desckind,
                              BINDPTR& bindptr)
{
  if(desckind == DESCKIND_TYPECOMP && bindptr.lptcomp)
    bindptr.lptcomp->Release();
  else if(info)
  {
    if(desckind == DESCKIND_FUNCDESC && bindptr.lpfuncdesc)
      info->ReleaseFuncDesc(bindptr.lpfuncdesc);
    else if((desckind == DESCKIND_VARDESC ||
             desckind == DESCKIND_IMPLICITAPPOBJ) && bindptr.lpvardesc)
      info->ReleaseVarDesc(bindptr.lpvardesc);
  }

  COM_RELEASE(info);
}

bool tLuaCOM::getFUNCDESC(const char *name, FuncInfo& funcinfo)
{
  funcinfo.name = NULL;
  funcinfo.propget = NULL;
  funcinfo.propget_owner = NULL;
  funcinfo.propput = NULL;
  funcinfo.propput_owner = NULL;
  funcinfo.func = NULL;
  funcinfo.func_owner = NULL;

  // First, tries to see we have the FUNCDESC's cached

  size_t counter = 0;

  for(counter = 0; counter < mFuncInfo.size(); counter++)
  {
    if(strcmp(name, mFuncInfo[counter].name) == 0)
      break;
  }

  // checks whether funcinfo was found
  if(counter < mFuncInfo.size())
  {
    funcinfo = mFuncInfo[counter];
    return true;
  }

  // did not find, so gets type information through
  // ITypeComp

  HRESULT hr = S_OK;
  BINDPTR bindptr;
  DESCKIND desckind = DESCKIND_NONE;
  BSTR wName;
  ITypeInfo *info = NULL;

  unsigned int dumb = 0;

  wName = tUtil::string2bstr(name);

  unsigned long lhashval = LHashValOfName(LOCALE_SYSTEM_DEFAULT, wName);

  ZeroMemory(&bindptr, sizeof(bindptr));
  desckind = DESCKIND_NONE;
  info = NULL;
  hr = ptcomp->Bind(wName, lhashval, INVOKE_PROPERTYGET,
    &info, &desckind, &bindptr);
  
  if(FAILED(hr) || desckind != DESCKIND_FUNCDESC)
  {
    funcinfo.propget = NULL;
    ReleaseBindResult(info, desckind, bindptr);
  }
  else
  {
    funcinfo.propget = bindptr.lpfuncdesc;
    funcinfo.propget_owner = info;
  }

  ZeroMemory(&bindptr, sizeof(bindptr));
  desckind = DESCKIND_NONE;
  info = NULL;
  hr = ptcomp->Bind(wName, lhashval, INVOKE_FUNC,
    &info, &desckind, &bindptr);
  
  if(FAILED(hr) || desckind != DESCKIND_FUNCDESC)
  {
    funcinfo.func = NULL;
    ReleaseBindResult(info, desckind, bindptr);
  }
  else
  {
    funcinfo.func = bindptr.lpfuncdesc;
    funcinfo.func_owner = info;
  }


  ZeroMemory(&bindptr, sizeof(bindptr));
  desckind = DESCKIND_NONE;
  info = NULL;
  hr = ptcomp->Bind(wName, lhashval, INVOKE_PROPERTYPUT,
    &info, &desckind, &bindptr);
  
  if(FAILED(hr) || desckind != DESCKIND_FUNCDESC)
  {
    funcinfo.propput = NULL;
    ReleaseBindResult(info, desckind, bindptr);
  }
  else
  {
    funcinfo.propput = bindptr.lpfuncdesc;
    funcinfo.propput_owner = info;
  }

  // if there is not propertyput, then tries propputref

  if(funcinfo.propput == NULL)
  {
    ZeroMemory(&bindptr, sizeof(bindptr));
    desckind = DESCKIND_NONE;
    info = NULL;
    hr = ptcomp->Bind(wName, lhashval, INVOKE_PROPERTYPUTREF,
      &info, &desckind, &bindptr);

    if(FAILED(hr) || desckind != DESCKIND_FUNCDESC)
    {
      funcinfo.propput = NULL;
      ReleaseBindResult(info, desckind, bindptr);
    }
    else
    {
      funcinfo.propput = bindptr.lpfuncdesc;
      funcinfo.propput_owner = info;
    }
  }

  SysFreeString(wName);

  // If no type information found, returns NULL
  if(!funcinfo.propget && !funcinfo.propput && !funcinfo.func)
    return false;
  else
  {
    FuncInfo cached = funcinfo;
    cached.name = tUtil::strdup(name);
    if(!cached.name)
    {
      ReleaseFuncDesc(cached.propget_owner, cached.propget);
      ReleaseFuncDesc(cached.propput_owner, cached.propput);
      ReleaseFuncDesc(cached.func_owner, cached.func);
      CHKMALLOC(cached.name);
    }

    try
    {
      mFuncInfo.push_back(cached);
    }
    catch(...)
    {
      free(cached.name);
      ReleaseFuncDesc(cached.propget_owner, cached.propget);
      ReleaseFuncDesc(cached.propput_owner, cached.propput);
      ReleaseFuncDesc(cached.func_owner, cached.func);
      throw;
    }

    return true;
  }
}


//
// Tries to find a constant in the type library and
// pushes its value to Lua
//
bool tLuaCOM::getConstant(lua_State* L, const char* name)
{
  if(plib_tcomp == NULL)
    return false;

  HRESULT hr = S_OK;
  BINDPTR bindptr;
  ZeroMemory(&bindptr, sizeof(bindptr));
  DESCKIND desckind = DESCKIND_NONE;
  BSTR wName;
  bool result = false;

  unsigned int dumb = 0;

  wName = tUtil::string2bstr(name);

  unsigned long lhashval = LHashValOfName(LOCALE_SYSTEM_DEFAULT, wName);

  ITypeInfo* info = NULL;
  hr = plib_tcomp->Bind(wName, lhashval, INVOKE_PROPERTYGET,
    &info, &desckind, &bindptr);

  SysFreeString(wName);

  try
  {
    if(SUCCEEDED(hr)
      && desckind == DESCKIND_VARDESC
      && bindptr.lpvardesc->varkind == VAR_CONST)
    {
      typehandler->com2lua(L, *bindptr.lpvardesc->lpvarValue);
      result = true;
    }
  }
  catch(...)
  {
    ReleaseBindResult(info, desckind, bindptr);
    throw;
  }

  ReleaseBindResult(info, desckind, bindptr);

  return result;
}

bool tLuaCOM::getDISPID(const char* name, DISPID* dispid)
{
   checkComObject();
   tCOMPtr<IDispatch> active_dispatch(pdisp);

   HRESULT hr;
   wchar_t* w_name = (wchar_t*) malloc( (strlen(name) + 1) * sizeof(wchar_t));
   mbstowcs(w_name,name,strlen(name)+1);

   hr = active_dispatch->GetIDsOfNames(IID_NULL, &w_name, 1,
                          LOCALE_SYSTEM_DEFAULT,dispid);
   free(w_name);
   
   return SUCCEEDED(hr);
}

int tLuaCOM::call(lua_State* L,
                  DISPID dispid,
                  int invkind,
                  FUNCDESC *pfuncdesc,
                  tLuaObjList params)
{
  checkComObject();
  const int stack_top = lua_gettop(L);
  if(!lua_checkstack(L, stack_top + 2))
    LUACOM_EXCEPTION(MALLOC_ERROR);

  int status;
  std::exception_ptr exception;
  {
    CallContext context(this, dispid, invkind, pfuncdesc, params);

    // Prepare the protected frame before retaining any COM resources.
    lua_pushlightuserdata(L, &context);
    lua_pushcclosure(L, protectedCall, 1);
    for(int i = 1; i <= stack_top; i++)
      lua_pushvalue(L, i);

    try
    {
      // Lua allocation can run finalizers while the frame is prepared.
      checkComObject();
      context.active_dispatch = pdisp;
      status = lua_pcall(L, stack_top, LUA_MULTRET, 0);
      exception = context.exception;
    }
    catch(...)
    {
      lua_settop(L, stack_top);
      throw;
    }
  }

  // Preserve both C++ exceptions and Lua error values after COM cleanup.
  if(exception)
  {
    lua_settop(L, stack_top);
    std::rethrow_exception(exception);
  }
  if(status != 0)
    return lua_error(L);
  return lua_gettop(L) - stack_top;
}

int tLuaCOM::protectedCall(lua_State* L)
{
  CallContext* context = static_cast<CallContext*>(
    lua_touserdata(L, lua_upvalueindex(1)));
  try
  {
    return context->object->callWithDispatch(L, *context);
  }
  catch(const tLuaCOMException&)
  {
    context->exception = std::current_exception();
    return 0;
  }
  catch(const std::exception&)
  {
    context->exception = std::current_exception();
    return 0;
  }
}

int tLuaCOM::callWithDispatch(lua_State* L, CallContext& context)
{
  tUtil::log_verbose("tLuaCOM.call", "about to call DISPID 0x%.8x", context.dispid);

  UINT ArgErr = 0;
  int num_retvals = 0;
  DISPPARAMS& dispparams = context.dispparams;
  VARIANTARG& result = context.result;
  EXCEPINFO& excepinfo = context.excepinfo;
  FUNCDESC* pfuncdesc = context.funcdesc;

  typehandler->fillDispParams(L, dispparams, pfuncdesc,
                              context.params, context.invkind);

  HRESULT hr = context.active_dispatch->Invoke(
    context.dispid,
    IID_NULL,
    LOCALE_SYSTEM_DEFAULT,
    context.invkind,
    &dispparams,
    &result,
    &excepinfo,
    &ArgErr);

  if(SUCCEEDED(hr))
  {
    // Untyped methods always return a value; empty COM values become nil.
    if((pfuncdesc && pfuncdesc->elemdescFunc.tdesc.vt != VT_VOID) || !pfuncdesc)
    {
      typehandler->com2lua(L, result);
      num_retvals++;
    }

    if(context.invkind & INVOKE_FUNC)
      num_retvals += typehandler->pushOutValues(L, dispparams, pfuncdesc);

    return num_retvals;
  }

  if(hr == DISP_E_EXCEPTION)
  {
    if(excepinfo.pfnDeferredFillIn != NULL)
    {
      HRESULT fill_result = excepinfo.pfnDeferredFillIn(&excepinfo);
      CHK_COM_CODE(fill_result);
    }

    if(excepinfo.bstrDescription != NULL)
      COM_EXCEPTION(tUtil::bstr2string(excepinfo.bstrDescription));
    else if(excepinfo.wCode != 0)
      COM_EXCEPTION(tUtil::GetErrorMessage(excepinfo.wCode));
    else if(excepinfo.scode != 0)
      COM_EXCEPTION(tUtil::GetErrorMessage(excepinfo.scode));
    else
      COM_EXCEPTION("Unknown exception");
  }
  else
    COM_ERROR(tUtil::GetErrorMessage(hr));
}

DWORD tLuaCOM::addConnection(tLuaCOM *server)
{
  checkComObject();
  tCOMPtr<IDispatch> active_dispatch(pdisp);

  if(!server)
    return 0;
  if(!server->hasTypeInfo())
    return false;

  HRESULT hr;
  tCOMPtr<IDispatch> pdisp_server(server->GetIDispatch());
  if(!pdisp_server)
    return 0;

  tCOMPtr<IConnectionPointContainer> pcpc;

  tCOMPtr<IConnectionPoint> connection_point;

  hr = active_dispatch->QueryInterface
    (
      IID_IConnectionPointContainer, (void **) &pcpc
    );

  if(FAILED(hr) || !pcpc)
  {
    return 0;
  }

  IID guid;
  server->GetIID(&guid);
  hr = pcpc->FindConnectionPoint(guid, &connection_point);

  if(FAILED(hr) || !connection_point)
  {
    return 0;
  }

  // Allocate the record before Advise can establish a connection.
  ConnectionList pending;
  try
  {
    pending.push_back(Connection(connection_point, guid));
  }
  catch(const std::bad_alloc&)
  {
    return 0;
  }
  DWORD connection_point_cookie = 0;

  // Metadata calls can release this wrapper before registration begins.
  checkComObject();
  hr = connection_point->Advise
    (
      (IUnknown *) pdisp_server, &connection_point_cookie
    );

  if(FAILED(hr) || connection_point_cookie == 0)
  {
    if(SUCCEEDED(hr))
      connection_point->Unadvise(connection_point_cookie);
    return 0;
  }

  pending.front().cookie = connection_point_cookie;
  if(!pdisp)
  {
    // A callback released the wrapper while Advise was running.
    hr = connection_point->Unadvise(connection_point_cookie);
    if(FAILED(hr))
      connections.splice(connections.end(), pending);
    CHK_COM_CODE(hr);
    checkComObject();
  }
  connections.splice(connections.end(), pending);
  last_connection_interface = guid;
  last_connection_cookie = connection_point_cookie;

  return connection_point_cookie;
}

void tLuaCOM::releaseConnection()
{
  // The legacy overload releases only the last connection made. Keep its
  // identity after removal so repeated calls cannot release older connections.
  ConnectionList pending;
  for(ConnectionList::iterator it = connections.begin();
      it != connections.end(); ++it)
  {
    if(it->interface_id == last_connection_interface &&
       it->cookie == last_connection_cookie)
    {
      pending.splice(pending.end(), connections, it);
      break;
    }
  }
  if(pending.empty())
    return;

  HRESULT hr = pending.front().point->Unadvise(pending.front().cookie);
  if(FAILED(hr))
    connections.splice(connections.end(), pending);
  CHK_COM_CODE(hr);
}

void tLuaCOM::releaseConnection(tLuaCOM* server, DWORD cookie)
{
  checkComObject();
  tCOMPtr<IDispatch> active_dispatch(pdisp);

  tCOMPtr<IConnectionPointContainer> pcpc;

  tCOMPtr<IConnectionPoint> connection_point;

  HRESULT hr = active_dispatch->QueryInterface
    (
      IID_IConnectionPointContainer, (void **) &pcpc
    );

  if(FAILED(hr) || !pcpc)
  {
    LUACOM_ERROR("Object does not accept connections!");
  }

  IID guid;
  server->GetIID(&guid);
  hr = pcpc->FindConnectionPoint(guid, &connection_point);

  if(FAILED(hr) || !connection_point)
  {
    LUACOM_ERROR("No connection point for this interface!");
  }

  // Cookies are unique only within one connection point. Detach the matching
  // record before Unadvise can call back into Lua, and restore it on failure.
  ConnectionList pending;
  for(ConnectionList::iterator it = connections.begin();
      it != connections.end(); ++it)
  {
    if(it->interface_id == guid && it->cookie == cookie)
    {
      pending.splice(pending.end(), connections, it);
      break;
    }
  }

  hr = connection_point->Unadvise(cookie);
  if(FAILED(hr))
    connections.splice(connections.end(), pending);
  CHK_COM_CODE(hr);
}

HRESULT tLuaCOM::releaseConnections() {
  ConnectionList pending;
  pending.splice(pending.end(), connections);
  HRESULT result = S_OK;
  for(ConnectionList::iterator it = pending.begin(); it != pending.end(); )
  {
    ConnectionList::iterator current = it++;
    HRESULT hr = current->point->Unadvise(current->cookie);
    if(FAILED(hr))
    {
      if(SUCCEEDED(result))
        result = hr;
      // Keep failed cleanup available for an explicit retry.
      connections.splice(connections.end(), pending, current);
    }
  }
  return result;
}

//
//  isMember
//
//    Informa se existe algum metodo ou propriedade com
//    o nome passado como parametro
//

bool tLuaCOM::isMember(const char * name)
{
  checkComObject();
  tCOMPtr<IDispatch> active_dispatch(pdisp);

  HRESULT hr;
  DISPID dumb_dispid;
  
  wchar_t* w_name = (wchar_t*) malloc( (strlen(name) + 1) * sizeof(wchar_t));

  assert(w_name);
  if(!w_name)
    return false;

  mbstowcs(w_name, name, strlen(name)+1);

  hr = active_dispatch->GetIDsOfNames(IID_NULL, &w_name, 1,
                        LOCALE_SYSTEM_DEFAULT, &dumb_dispid);
  free(w_name);
  w_name = NULL;

  if(!FAILED(hr))
    return true;
  else
    return false;
}


void tLuaCOM::getHelpInfo(char **ppHelpFile, DWORD *pHelpContext)
{
  *ppHelpFile = NULL;
  *pHelpContext = 0;
  if(!hasTypeInfo())
    return;

  tCOMPtr<ITypeLib> typelib;
  BSTR helpfile = NULL;
  HRESULT hr = S_OK;
  
  hr = ptinfo->GetDocumentation(-1, NULL, NULL, pHelpContext, &helpfile);

  if(FAILED(hr) || helpfile == NULL)
  {
    SysFreeString(helpfile);
    return;
  }

  // Se nao conseguiu help contextna propria interface, tenta obte-lo
  // na type library
  if(*pHelpContext == 0)
  {
    unsigned int dumb_index = 0;
    DWORD typelib_help_context = 0;
    BSTR helpfile_typelib = NULL;

    hr = ptinfo->GetContainingTypeLib(&typelib, &dumb_index);

    if(!FAILED(hr))
    {
      hr = typelib->GetDocumentation(-1, NULL, NULL,
        &typelib_help_context, &helpfile_typelib);

      if(!FAILED(hr) && helpfile_typelib != NULL)
      {
        SysFreeString(helpfile);

        helpfile = helpfile_typelib;
        *pHelpContext = typelib_help_context;
      }
      else
        SysFreeString(helpfile_typelib);
    }
  }

  int str_size = WideCharToMultiByte(CP_ACP, 0, helpfile, -1,
                                     NULL, 0, NULL, NULL);
  if(str_size <= 0)
  {
    SysFreeString(helpfile);
    LUACOM_EXCEPTION(WINDOWS_ERROR);
  }

  char * result = (char *) malloc(str_size);
  if(!result)
  {
    SysFreeString(helpfile);
    LUACOM_EXCEPTION(MALLOC_ERROR);
  }
  if(WideCharToMultiByte(CP_ACP, 0, helpfile, -1, result,
                         str_size, NULL, NULL) != str_size)
  {
    free(result);
    SysFreeString(helpfile);
    LUACOM_EXCEPTION(WINDOWS_ERROR);
  }
  *ppHelpFile = result;

  SysFreeString(helpfile);
}



//
// CreateLuaCOM
//


tLuaCOM * tLuaCOM::CreateLuaCOM(lua_State* L,
                                IDispatch * pdisp,
                                const CLSID& coclass,
                                ITypeInfo* typeinfo,
                                bool untyped,
				const char* name
                                )
{
  HRESULT hr = S_OK;

  CHECKPARAM(pdisp && L);
  CHECKPARAM(!untyped || !typeinfo);

  if(!untyped)
  {
    if(!typeinfo)
      typeinfo = tCOMUtil::GetDispatchTypeInfo(pdisp); 
    else
      typeinfo->AddRef();
  }

  char * object_name = NULL;
  if(name)
  {
    object_name = _strdup(name);
    if(!object_name)
    {
      COM_RELEASE(typeinfo);
      LUACOM_EXCEPTION(MALLOC_ERROR);
    }
  }

  tLuaCOM *lcom = NULL;
  try
  {
    lcom = new tLuaCOM(L, pdisp, typeinfo, coclass);
  }
  catch (const std::bad_alloc&)
  {
    free(object_name);
    COM_RELEASE(typeinfo);
    LUACOM_EXCEPTION(MALLOC_ERROR);
  }
  catch (...)
  {
    free(object_name);
    COM_RELEASE(typeinfo);
    throw;
  }

  lcom->objName = object_name;
  COM_RELEASE(typeinfo);

  // We have one reference (the pointer), so we lock the object
  lcom->Lock();

  return lcom;
}

ITypeInfo * tLuaCOM::GetDefaultEventsInterface()
{
  checkComObject();
  tCOMPtr<IDispatch> active_dispatch(pdisp);

  CLSID clsid = GetCLSID();
  if(clsid == IID_NULL)
    return NULL;
  
  tCOMPtr<ITypeInfo> coclassinfo;
  coclassinfo.Attach(tCOMUtil::GetCoClassTypeInfo(active_dispatch, clsid));
  if(!coclassinfo)
    return NULL;
  
  ITypeInfo *ptinfo = tCOMUtil::GetDefaultInterfaceTypeInfo(coclassinfo, true);

  return ptinfo;
}

void tLuaCOM::ReleaseFuncDesc(ITypeInfo *owner, FUNCDESC *pfuncdesc)
{
  if(pfuncdesc && owner)
    owner->ReleaseFuncDesc(pfuncdesc);
  COM_RELEASE(owner);
}

IDispatch * tLuaCOM::GetIDispatch()
{
  checkComObject();
  return pdisp;
}

void tLuaCOM::GetIID(IID * piid)
{
  CHECKPRECOND(ptinfo);

  TYPEATTR *ptypeattr = NULL;

  CHK_COM_CODE(ptinfo->GetTypeAttr(&ptypeattr));
  CHK_LCOM_ERR(ptypeattr, "Type information attributes are unavailable.");
  *piid = ptypeattr->guid;
  ptinfo->ReleaseTypeAttr(ptypeattr);
}


CLSID tLuaCOM::GetCLSID()
{
  HRESULT hr = S_OK;

  if(clsid != IID_NULL)
    return clsid;

  checkComObject();
  tCOMPtr<IDispatch> active_dispatch(pdisp);

  // tries to find the CLSID using IProvideClassInfo
  tCOMPtr<ITypeInfo> coclassinfo;
  coclassinfo.Attach(tCOMUtil::GetCoClassTypeInfo(active_dispatch));
  if(coclassinfo)
  {
    clsid = tCOMUtil::GetCLSID(coclassinfo);
    if(clsid != IID_NULL)
      return clsid;
  }

  // Now searches the type library seeking the coclass to which
  // this interface belongs
  clsid = tCOMUtil::FindCLSID(ptinfo);

  return clsid;
}


ITypeInfo* tLuaCOM::GetTypeInfo()
{
  return ptinfo;
}

int tLuaCOM::Lock()
{
  return ++lock_count;
}

int tLuaCOM::Unlock()
{
  if(--lock_count == 0)
  {
    delete this;
    return 0;
  }
  else
    return lock_count;
}


bool tLuaCOM::hasTypeInfo(void)
{
  if(ptinfo)
    return true;
  else
    return false;
}

void tLuaCOM::checkComObject() const
{
  if(!pdisp)
    LUACOM_ERROR("COM object has been released.");
}

// Releases the reference owned by this wrapper. The wrapper remains valid so
// Lua garbage collection can destroy it safely, but it cannot call COM again.
void tLuaCOM::releaseComObject() {
  // Detach before COM cleanup can call back into this wrapper.
  tCOMPtr<IDispatch> released_dispatch;
  released_dispatch.Attach(pdisp.Detach());
  CHK_COM_CODE(releaseConnections());
}
