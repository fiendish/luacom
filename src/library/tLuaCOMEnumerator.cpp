/**
  tLuaCOMEnumerator.cpp: tLuaCOMEnumerator class.
*/

#include "tLuaCOMEnumerator.h"
#include <math.h>
#include <limits.h>
#include <exception>

#include "tUtil.h"
#include "tCOMUtil.h"
#include "tLuaCOMException.h"

#include "LuaAux.h"

#include "luacom_internal.h"
#include "LuaCompat.h"


// initialization

const char *tLuaCOMEnumerator::type_name = "__ENUMERATOR_LUACOM_TYPE"; 
const char *tLuaCOMEnumerator::pointer_type_name = "__ENUMERATOR_POINTER_LUACOM_TYPE"; 
// the address of each static is a unique memory location used as the key to the lua registry
// see http://www.lua.org/pil/27.3.1.html
const char tLuaCOMEnumerator::module_name_key = 'k';

#define ENUMERATOR_FIELD "__TLUACOMENUMERATOR__internal"


tLuaCOMEnumerator::tLuaCOMEnumerator(IEnumVARIANT* pEV_param)
{
  CHECKPRECOND(pEV_param);
  
  // stores enumerator
  pEV.Attach(pEV_param);
  pEV->AddRef();

  typehandler = new tLuaCOMTypeHandler(NULL);
}

tLuaCOMEnumerator::~tLuaCOMEnumerator()
{
  delete typehandler;
}

int tLuaCOMEnumerator::index(lua_State *L)
{
  // pushes connection point
  lua_pushstring(L, ENUMERATOR_FIELD);
  lua_gettable(L, -3);

  // pushes method name
  lua_pushvalue(L, 2);

  // pushes closure to call all sinks
  lua_pushcclosure(L, call_method, 2);

  return 1;
}

void tLuaCOMEnumerator::push(lua_State* L)
{
  LUASTACK_SET(L);

  tStringBuffer module_name(tUtil::RegistryGetString(L, module_name_key));
  LUASTACK_DOCLEAN(L, 0);

  // creates table
  lua_newtable(L);
  luaCompat_pushTypeByName(L, 
    module_name, 
    tLuaCOMEnumerator::type_name);

  lua_setmetatable(L, -2);

  lua_pushstring(L, ENUMERATOR_FIELD);

  // pushes typed pointer
  luaCompat_pushTypeByName(L, 
    module_name, 
    tLuaCOMEnumerator::pointer_type_name);

  luaCompat_newTypedObject(L, this);

  // stores in the table
  lua_settable(L, -3);

  LUASTACK_CLEAN(L, 1);
}

int tLuaCOMEnumerator::garbagecollect(lua_State *L)
{
  LUASTACK_SET(L);

  // gets the enumerator
  tLuaCOMEnumerator* enumerator = 
    (tLuaCOMEnumerator*)*(void **)lua_touserdata(L, -1);

  delete enumerator;

  LUASTACK_CLEAN(L, 0);

  return 0;
}

void tLuaCOMEnumerator::registerLuaType(lua_State *L, const char *module)
{
  LUASTACK_SET(L);

  tStringBuffer module_name(module);
  // store value for later use (used to be DLL-static)
  tUtil::RegistrySetString(L, module_name_key, module_name);
  LUASTACK_CLEAN(L, 0);

  luaCompat_newLuaType(L, 
    module_name, 
    tLuaCOMEnumerator::type_name);

  luaCompat_newLuaType(L, 
    module_name, 
    tLuaCOMEnumerator::pointer_type_name);

  luaCompat_pushTypeByName(L, 
    module_name, 
    tLuaCOMEnumerator::type_name);

  lua_pushcfunction(L, tLuaCOMEnumerator::index);
  lua_setfield(L, -2, "__index");

  lua_pop(L, 1);

  luaCompat_pushTypeByName(L, 
    module_name, 
    tLuaCOMEnumerator::pointer_type_name);

  lua_pushcfunction(L, tLuaCOMEnumerator::garbagecollect);
  lua_setfield(L, -2, "__gc");

  lua_pop(L, 1);
  
  LUASTACK_CLEAN(L, 0);
}


int tLuaCOMEnumerator::call_method(lua_State *L)
{
  /// positions of parameters
  
  // self param (not used, but explicited to ensure
  // consistency)
  const int self_param        = 1;

  // first user param 
  const int user_first_param  = 2;
  
  // last user param
  const int user_last_param   = lua_gettop(L);

  // upvalues
  const int enumerator_param  = lua_upvalueindex(1);
  const int method_param      = lua_upvalueindex(2);

  int num_params = 0;

  if(user_last_param < user_first_param)
    num_params = 0;
  else
    num_params = user_last_param - user_first_param + 1;

  // gets the enumerator
  tLuaCOMEnumerator* enumerator = 
    (tLuaCOMEnumerator*)*(void **)lua_touserdata(L, enumerator_param);

  // gets the method name
  const char* method_name = lua_tostring(L, method_param);

  // call method
  int retval = 0;
  bool failed = false;
  try
  {
    retval = enumerator->callCOMmethod(L, method_name, user_first_param, num_params);
  }
  catch(class tLuaCOMException& e)
  {
    lua_pushstring(L, e.getMessage());
    failed = true;
  }

  // Destroy the exception and its formatted temporary before Lua can longjmp.
  if(failed)
  {
    luacom_error(L, lua_tostring(L, -1));
    return 0;
  }
  return retval;
}

struct tLuaCOMEnumerator::NextContext
{
  explicit NextContext(tLuaCOMTypeHandler* handler)
    : handler(handler), values(NULL), count(0), fetched(0) {}
  ~NextContext()
  {
    for(ULONG i = 0; i < count; i++)
      handler->releaseVariant(&values[i]);
    delete[] values;
  }
  tLuaCOMTypeHandler* handler;
  VARIANT* values;
  ULONG count, fetched;
  std::exception_ptr exception;
};

int tLuaCOMEnumerator::protectedNext(lua_State* L)
{
  NextContext* context = static_cast<NextContext*>(
    lua_touserdata(L, lua_upvalueindex(1)));
  if(context->fetched > INT_MAX ||
     !lua_checkstack(L, static_cast<int>(context->fetched)))
    return luaL_error(L, "Insufficient Lua stack space for enumeration results");
  try
  {
    for(ULONG i = 0; i < context->fetched; i++)
      context->handler->com2lua(L, context->values[i]);
    return static_cast<int>(context->fetched);
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

int tLuaCOMEnumerator::callCOMmethod(lua_State* L, const char *name, int first_param, int num_params)
{
  HRESULT hr = S_OK;

  // Next method
  if(strcmp(name, "Next") == 0)
  {
    unsigned long num_elements = 1;
    if(num_params > 0)
    {
      lua_Number requested = lua_tonumber(L, first_param);
      CHECKPARAM(requested >= 0 &&
                 requested <= static_cast<lua_Number>(ULONG_MAX) &&
                 floor(requested) == requested);
      num_elements = static_cast<unsigned long>(requested);
    }

    if(num_elements == 0)
      return 0;

    const int top = lua_gettop(L);
    if(!lua_checkstack(L, 2))
      LUACOM_EXCEPTION(MALLOC_ERROR);

    int status;
    std::exception_ptr exception;
    {
      NextContext context(typehandler);
      // Prepare the protected worker before acquiring the result array.
      lua_pushlightuserdata(L, &context);
      lua_pushcclosure(L, protectedNext, 1);
      try
      {
        context.values = new VARIANT[num_elements];
      }
      catch(const std::bad_alloc&)
      {
        LUACOM_EXCEPTION(MALLOC_ERROR);
      }
      for(ULONG i = 0; i < num_elements; i++)
        VariantInit(&context.values[i]);
      context.count = num_elements;

      hr = pEV->Next(num_elements, context.values, &context.fetched);
      if(context.fetched > num_elements)
        LUACOM_EXCEPTION(INTERNAL_ERROR);
      if(FAILED(hr)) // S_FALSE is a successful partial/end-of-enumeration result.
        CHK_COM_CODE(hr);

      status = lua_pcall(L, 0, LUA_MULTRET, 0);
      exception = context.exception;
    }
    if(exception)
    {
      lua_settop(L, top);
      std::rethrow_exception(exception);
    }
    if(status != 0)
      return lua_error(L);
    return lua_gettop(L) - top;
  }

  if(strcmp(name, "Reset") == 0)
  {
    hr = pEV->Reset();
    CHK_LCOM_ERR(hr == S_OK, "Unable to reset enumeration.");
    
    return 0;
  }

  if(strcmp(name, "Skip") == 0)
  {
    CHK_LCOM_ERR(num_params > 0, "Not enough parameters.");

    lua_Number requested = lua_tonumber(L, first_param);
    CHECKPARAM(requested >= 0 &&
               requested <= static_cast<lua_Number>(ULONG_MAX) &&
               floor(requested) == requested);
    unsigned long num_elements = static_cast<unsigned long>(requested);

    hr = pEV->Skip(num_elements);

    lua_pushboolean(L, hr == S_OK);
    return 1;
  }

  if(strcmp(name, "Clone") == 0)
  {
    tCOMPtr<IEnumVARIANT> p_newEV;
    CHK_COM_CODE(pEV->Clone(&p_newEV));

    tLuaCOMEnumerator* enumerator = NULL;
    try
    {
      enumerator = new tLuaCOMEnumerator(p_newEV);
    }
    catch (...)
    {
      LUACOM_EXCEPTION(MALLOC_ERROR);
    }
    
    enumerator->push(L);
    return 1;
  }

  return 0;
}
