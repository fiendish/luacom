/**
  tLuaTLB.cpp: implementation of the tLuaTLB class.
*/

#include "tLuaTLB.h"

#include "tCOMUtil.h"
#include "tLuaCOMException.h"
#include "tUtil.h"
#include "luacom_internal.h"
#include "LuaAux.h"

#include "tLuaCOM.h"
#include "tLuaCOMTypeHandler.h"
#include "LuaCompat.h"

// dynamic_cast if RTTI enabled.
// static_cast  if RTTI disabled.
#ifdef _CPPRTTI   // MSVC RTTI
#define DYNAMIC_CAST dynamic_cast
#else
#define DYNAMIC_CAST static_cast
#endif

namespace
{
class tFuncDescHolder
{
public:
  tFuncDescHolder(ITypeInfo * typeinfo, FUNCDESC * value)
    : m_typeinfo(typeinfo), m_value(value) {}
  ~tFuncDescHolder()
    { if(m_value) m_typeinfo->ReleaseFuncDesc(m_value); }
private:
  ITypeInfo * m_typeinfo;
  FUNCDESC * m_value;
};

class tVarDescHolder
{
public:
  tVarDescHolder(ITypeInfo * typeinfo, VARDESC * value)
    : m_typeinfo(typeinfo), m_value(value) {}
  ~tVarDescHolder()
    { if(m_value) m_typeinfo->ReleaseVarDesc(m_value); }
private:
  ITypeInfo * m_typeinfo;
  VARDESC * m_value;
};

class tTypeAttrHolder
{
public:
  tTypeAttrHolder(ITypeInfo * typeinfo, TYPEATTR * value)
    : m_typeinfo(typeinfo), m_value(value) {}
  ~tTypeAttrHolder()
    { if(m_value) m_typeinfo->ReleaseTypeAttr(m_value); }
private:
  ITypeInfo * m_typeinfo;
  TYPEATTR * m_value;
};

class tBstrHolder
{
public:
  tBstrHolder() : m_value(NULL) {}
  ~tBstrHolder() { SysFreeString(m_value); }
  BSTR * out() { return &m_value; }
  BSTR get() const { return m_value; }
private:
  BSTR m_value;
};

class tBstrArrayHolder
{
public:
  explicit tBstrArrayHolder(UINT count)
    : m_values(new BSTR[count]), m_count(count)
  {
    for(UINT i = 0; i < m_count; ++i)
      m_values[i] = NULL;
  }
  ~tBstrArrayHolder()
  {
    for(UINT i = 0; i < m_count; ++i)
      SysFreeString(m_values[i]);
    delete[] m_values;
  }
  BSTR * data() { return m_values; }
  BSTR operator[](UINT index) const { return m_values[index]; }
private:
  BSTR * m_values;
  UINT m_count;
};
}

const char tLuaTLB::type_name[] = "ITypeLib";
const char tLuaTLB::pointer_type_name[] = "ITypeLib_pointer";

const char tLuaTypeInfo::type_name[] = "ITypeInfo";
const char tLuaTypeInfo::pointer_type_name[] = "ITypeInfo_pointer";


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

tLuaTLB::tLuaTLB(lua_State* L, ITypeLib *p_typelib)
{
  method_table.Add("ShowHelp", ShowHelp, FUNC);
  method_table.Add("GetDocumentation", GetDocumentation, FUNC);
  method_table.Add("GetTypeInfo", GetTypeInfo, FUNC);
  method_table.Add("GetTypeInfoCount", GetTypeInfoCount, FUNC);
  method_table.Add("ExportConstants", ExportConstants, FUNC);
  method_table.Add("ExportEnumerations", ExportEnumerations, FUNC);

  typelib.Attach(p_typelib);
  typelib->AddRef();
}

tLuaTLB::~tLuaTLB()
{
}

int tLuaTLB::index(lua_State* L)
{
  return 0;
}

int tLuaTLB::pushNew(lua_State *L, ITypeLib *p_typelib)
{
  CHECKPRECOND(p_typelib);

  // tests whether we have a cached instance
  if(pushCachedObject(L, p_typelib))
    return 1;

  LUASTACK_SET(L);

  tLuaTLB* lua_tlb = new tLuaTLB(L, p_typelib);

  int retval = generic_PushNew(L, lua_tlb, type_name, pointer_type_name);
  cacheObject(L, (void*) p_typelib);

  LUASTACK_CLEAN(L, 1);

  return retval;
}

int tLuaTLB::GetTypeInfoCount(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  
  CHECKPRECOND(lua_tlb);

  int count = lua_tlb->typelib->GetTypeInfoCount();

  lua_pushnumber(L, count);
  return 1;
}

int tLuaTLB::GetTypeInfo(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  CHECKPRECOND(lua_tlb);

  UINT typeinfo_pos = (UINT)lua_tointeger(L, 2);

  tCOMPtr<ITypeInfo> typeinfo;
  CHK_COM_CODE(lua_tlb->typelib->GetTypeInfo(typeinfo_pos , &typeinfo));

  tLuaTypeInfo::pushNew(L, typeinfo);
  return 1;
}

int tLuaTLB::GetDocumentation(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  
  CHECKPRECOND(lua_tlb);

  tBstrHolder name;
  tBstrHolder helpstring;
  ULONG helpcontext = 0;
  tBstrHolder helpfile;
  CHK_COM_CODE(lua_tlb->typelib->GetDocumentation(-1, 
    name.out(), helpstring.out(), &helpcontext, helpfile.out()));

  tStringBuffer name_text(tUtil::bstr2string(name.get()));
  tStringBuffer help_text(tUtil::bstr2string(helpstring.get()));
  tStringBuffer help_file_text(tUtil::bstr2string(helpfile.get()));

  lua_newtable(L);

  lua_pushstring(L, "name");
  lua_pushstring(L, name_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpstring");
  lua_pushstring(L, help_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpfile");
  lua_pushstring(L, help_file_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpcontext");
  lua_pushnumber(L, helpcontext);
  lua_settable(L, -3);
 
  return 1;
}

int tLuaTLB::ShowHelp(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  CHECKPRECOND(lua_tlb);

  ULONG helpcontext = 0;
  tBstrHolder helpfile;
  CHK_COM_CODE(lua_tlb->typelib->GetDocumentation(
    -1, NULL, NULL, &helpcontext, helpfile.out()));
  
  tUtil::ShowHelp(tUtil::bstr2string(helpfile.get()), helpcontext);

  return 0;
}

int tLuaTLB::ExportConstants(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  CHECKPRECOND(lua_tlb);
  
  CHECKPARAM(lua_type(L, -1) == LUA_TTABLE);

  HRESULT hr = S_OK;

  long count = lua_tlb->typelib->GetTypeInfoCount();
  while(count--)
  {
    TYPEKIND tkind = TKIND_MAX;
    CHK_COM_CODE(lua_tlb->typelib->GetTypeInfoType(count, &tkind));

    if(tkind == TKIND_ENUM)
    {
      tCOMPtr<ITypeInfo> ptypeinfo;
      CHK_COM_CODE(lua_tlb->typelib->GetTypeInfo(count, &ptypeinfo));

      tLuaCOMTypeHandler typehandler(ptypeinfo);

      // get var_count
      TYPEATTR* ptypeattr = NULL;
      hr = ptypeinfo->GetTypeAttr(&ptypeattr);
      tTypeAttrHolder typeattr_holder(ptypeinfo, ptypeattr);
      CHK_COM_CODE(hr);
      CHK_LCOM_ERR(ptypeattr, "Type attributes are unavailable.");
      long var_count = ptypeattr->cVars;

      while(var_count--)
      {
        VARDESC* pvardesc = NULL;
        hr = ptypeinfo->GetVarDesc(var_count, &pvardesc);
        tVarDescHolder vardesc_holder(ptypeinfo, pvardesc);
        CHK_COM_CODE(hr);
        CHK_LCOM_ERR(pvardesc, "Variable description is unavailable.");

        tBstrHolder name;
        CHK_COM_CODE(ptypeinfo->GetDocumentation(
          pvardesc->memid, name.out(), NULL, NULL, NULL));
        CHK_LCOM_ERR(name.get(), "Variable name is unavailable.");

        lua_pushstring(L, tUtil::bstr2string(name.get()));

        typehandler.com2lua(L, *pvardesc->lpvarValue);

        // sets in the table
        lua_settable(L, -3);

      }
    }
  }

  return 1;
}


int tLuaTLB::ExportEnumerations(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTLB* lua_tlb = DYNAMIC_CAST<tLuaTLB*>(lua_obj);
  CHECKPRECOND(lua_tlb);

  if(lua_gettop(L) == 0)
    lua_newtable(L);
  else CHECKPARAM(lua_type(L, -1) == LUA_TTABLE);

  long count = lua_tlb->typelib->GetTypeInfoCount();
  while(count--)
  {
    TYPEKIND tkind = TKIND_MAX;
    CHK_COM_CODE(lua_tlb->typelib->GetTypeInfoType(count, &tkind));

    if(tkind == TKIND_ENUM)
    {
      tCOMPtr<ITypeInfo> ptypeinfo;
      CHK_COM_CODE(lua_tlb->typelib->GetTypeInfo(count, &ptypeinfo));

      tBstrHolder enum_name;
      CHK_COM_CODE(lua_tlb->typelib->GetDocumentation(
        count, enum_name.out(), NULL, NULL, NULL));
      CHK_LCOM_ERR(enum_name.get(), "Enumeration name is unavailable.");
      lua_pushstring(L, tUtil::bstr2string(enum_name.get()));
      lua_newtable(L);

      tLuaCOMTypeHandler typehandler(ptypeinfo);

      TYPEATTR* ptypeattr = NULL;
      HRESULT hr = ptypeinfo->GetTypeAttr(&ptypeattr);
      tTypeAttrHolder typeattr_holder(ptypeinfo, ptypeattr);
      CHK_COM_CODE(hr);
      CHK_LCOM_ERR(ptypeattr, "Type attributes are unavailable.");

      long var_count = ptypeattr->cVars;

      while(var_count--)
      {
        VARDESC* pvardesc = NULL;
        HRESULT hr = ptypeinfo->GetVarDesc(var_count, &pvardesc);
        tVarDescHolder vardesc_holder(ptypeinfo, pvardesc);
        CHK_COM_CODE(hr);
        CHK_LCOM_ERR(pvardesc, "Variable description is unavailable.");

        tBstrHolder name;
        CHK_COM_CODE(ptypeinfo->GetDocumentation(
          pvardesc->memid, name.out(), NULL, NULL, NULL));
        CHK_LCOM_ERR(name.get(), "Variable name is unavailable.");

        lua_pushstring(L, tUtil::bstr2string(name.get()));

        typehandler.com2lua(L, *pvardesc->lpvarValue);

        // sets in the table
        lua_settable(L, -3);

      }

      lua_settable(L, -3);

    }
  }

  return 1;
}



///
/// tLuaTypeInfo class
///



tLuaTypeInfo::tLuaTypeInfo(lua_State* L, ITypeInfo *p_typeinfo)
{
  method_table.Add("GetTypeLib", GetTypeLib, FUNC);
  method_table.Add("GetFuncDesc", GetFuncDesc, FUNC);
  method_table.Add("GetVarDesc", GetVarDesc, FUNC);
  method_table.Add("GetDocumentation", GetDocumentation, FUNC);
  method_table.Add("GetTypeAttr", GetTypeAttr, FUNC);
  method_table.Add("GetImplType", GetImplType, FUNC);
  method_table.Add("GetImplTypeFlags", GetImplTypeFlags, FUNC);
  
  typeinfo.Attach(p_typeinfo);
  typeinfo->AddRef();

  typehandler = new tLuaCOMTypeHandler(p_typeinfo);
}

tLuaTypeInfo::~tLuaTypeInfo()
{
  delete typehandler;
}


void tLuaTypeInfo::pushNew(lua_State *L, ITypeInfo *p_typeinfo)
{
  CHECKPRECOND(p_typeinfo);

  // tests whether we have a cached instance
  if(pushCachedObject(L, p_typeinfo))
    return;

  tLuaTypeInfo* lua_typeinfo = new tLuaTypeInfo(L, p_typeinfo);
  generic_PushNew(L, lua_typeinfo, type_name, pointer_type_name);
  cacheObject(L, (void*) p_typeinfo);

  return;
}

void tLuaTLB::Init(lua_State* L)
{
  RegisterType(L, type_name, pointer_type_name);
  tLuaTypeInfo::Init(L);
}


void tLuaTypeInfo::Init(lua_State* L)
{
  RegisterType(L, type_name, pointer_type_name);
}

int tLuaTypeInfo::GetFuncDesc(tLuaObject* lua_obj, lua_State* L)
{
  LUASTACK_SET(L);

  HRESULT hr = S_OK;

  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  // we just deal with oleautomation-compatible interfaces
  unsigned short flags = 0 ;
  {
    TYPEATTR* ptypeattr = NULL;
    hr = lua_typeinfo->typeinfo->GetTypeAttr(&ptypeattr);
    tTypeAttrHolder typeattr_holder(lua_typeinfo->typeinfo, ptypeattr);
    CHK_COM_CODE(hr);
    CHK_LCOM_ERR(ptypeattr, "Type attributes are unavailable.");

    flags = ptypeattr->wTypeFlags;

    if(!(flags & TYPEFLAG_FDISPATCHABLE))
      return 0;
  }

  // gets funcdesc

  FUNCDESC* pfuncdesc = NULL;
  {
    UINT i = (UINT)lua_tointeger(L, 2);

    hr = lua_typeinfo->typeinfo->GetFuncDesc(i, &pfuncdesc);
  }
  tFuncDescHolder funcdesc_holder(lua_typeinfo->typeinfo, pfuncdesc);
  CHK_COM_CODE(hr);
  CHK_LCOM_ERR(pfuncdesc, "Function description is unavailable.");

  if(pfuncdesc->wFuncFlags & FUNCFLAG_FRESTRICTED)
    return 0;

  // creates table to hold the funcdesc
  lua_newtable(L);

  // stores the various fields
  lua_pushstring(L, "memid");
  lua_pushnumber(L, pfuncdesc->memid);
  lua_settable(L, -3);

  lua_pushstring(L, "invkind");
  lua_pushstring(L, tCOMUtil::getPrintableInvokeKind(pfuncdesc->invkind));
  lua_settable(L, -3);

  lua_pushstring(L, "Params");
  lua_pushnumber(L, pfuncdesc->cParams);
  lua_settable(L, -3);

  lua_pushstring(L, "ParamsOpt");
  lua_pushnumber(L, pfuncdesc->cParamsOpt);
  lua_settable(L, -3);

  // gets all the names
  tBstrArrayHolder names(pfuncdesc->cParams+1);
  UINT found = 0;

  // gets the name of the functions and the names of the
  // parameters
  CHK_COM_CODE(lua_typeinfo->typeinfo->GetNames(
    pfuncdesc->memid, names.data(), pfuncdesc->cParams+1, &found));
  CHK_LCOM_ERR(found > 0 && names[0] != NULL,
               "Function name is unavailable.");

  lua_pushstring(L, "name");
  lua_pushstring(L, tUtil::bstr2string(names[0]));
  lua_settable(L, -3);

  // gets other documentation
  {
    tBstrHolder description;
    tBstrHolder helpfile;
    ULONG helpcontext = 0;

    CHK_COM_CODE(lua_typeinfo->typeinfo->GetDocumentation(
      pfuncdesc->memid, NULL, description.out(), &helpcontext, helpfile.out()));

    lua_pushstring(L, "description");
    lua_pushstring(L, tUtil::bstr2string(description.get()));
    lua_settable(L, -3);

    lua_pushstring(L, "helpfile");
    lua_pushstring(L, tUtil::bstr2string(helpfile.get()));
    lua_settable(L, -3);

    lua_pushstring(L, "helpcontext");
    lua_pushnumber(L, helpcontext);
    lua_settable(L, -3);

  }

  // now stores information for each parameter (if
  // this is an oleautomation interface

  // table to hold all parameters and stores it
  lua_newtable(L);
  lua_pushstring(L, "parameters");
  lua_pushvalue(L, -2);
  lua_settable(L, -4);

  SHORT i = 0;
  while(i < pfuncdesc->cParams)
  {
    // creates table to hold information for this parameter
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, i+1);

    // gets the name
    lua_pushstring(L, "name");

    if((i+1) < (SHORT)found)
    {
      lua_pushstring(L, tUtil::bstr2string(names[i+1]));
    }
    else
    {
      char buffer[50];
      sprintf(buffer, "p%d", i+1);
      lua_pushstring(L, buffer);
    }

    lua_settable(L, -3);

    // gets the type
    TYPEDESC tdesc = pfuncdesc->lprgelemdescParam[i].tdesc;
    bool incompatible_type = false;

    try
    {
      tdesc = 
        tLuaCOMTypeHandler::processTYPEDESC(lua_typeinfo->typeinfo, tdesc);
    }
    catch(class tLuaCOMException& e)
    {
      UNUSED(e);
      incompatible_type = true;
    }

    if(!incompatible_type)
    {
      lua_pushstring(L, "type");
      lua_pushstring(L, tCOMUtil::getPrintableTypeDesc(tdesc));
      lua_settable(L, -3);
    }

    USHORT paramFlags = pfuncdesc->lprgelemdescParam[i].paramdesc.wParamFlags;

    lua_pushstring(L, "in");
    lua_pushboolean(L,paramFlags & PARAMFLAG_FIN);
    lua_settable(L, -3);

    lua_pushstring(L, "out");
    lua_pushboolean(L,paramFlags & PARAMFLAG_FOUT);
    lua_settable(L, -3);

    lua_pushstring(L, "opt");
    lua_pushboolean(L,paramFlags & PARAMFLAG_FOPT);
    lua_settable(L, -3);

    lua_pushstring(L, "default");
    if(paramFlags & PARAMFLAG_FHASDEFAULT) {
      lua_typeinfo->typehandler->com2lua(L,pfuncdesc->lprgelemdescParam[i].paramdesc.pparamdescex->varDefaultValue);
    } else lua_pushnil(L);
    lua_settable(L, -3);

    // removes copy of the table
    lua_pop(L, 1);

    i++;
  }

  // removes copy of the table
  lua_pop(L, 1);

  // Stores the type for the return value
  TYPEDESC tdesc = pfuncdesc->elemdescFunc.tdesc;
  bool incompatible_type = false;

  try
  {
    tdesc = 
      tLuaCOMTypeHandler::processTYPEDESC(lua_typeinfo->typeinfo, tdesc);
  }
  catch(class tLuaCOMException&)
  {
    incompatible_type = true;
  }

  if(!incompatible_type)
  {
    lua_pushstring(L, "type");
    lua_pushstring(L, tCOMUtil::getPrintableTypeDesc(tdesc));
    lua_settable(L, -3);
  }

  LUASTACK_CLEAN(L, 1);

  return 1;
}


int tLuaTypeInfo::GetVarDesc(tLuaObject* lua_obj, lua_State* L)
{
  LUASTACK_SET(L);

  HRESULT hr = S_OK;

  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  // gets vardesc

  VARDESC* pvardesc = NULL;
  {
    UINT i = (UINT)lua_tointeger(L, 2);

    hr = lua_typeinfo->typeinfo->GetVarDesc(i, &pvardesc);
  }
  tVarDescHolder vardesc_holder(lua_typeinfo->typeinfo, pvardesc);
  CHK_COM_CODE(hr);
  CHK_LCOM_ERR(pvardesc, "Variable description is unavailable.");

  // table to hold the vardesc
  lua_newtable(L);

  // stores variable name
  lua_pushstring(L, "name");
  {
    tBstrHolder name;
    CHK_COM_CODE(lua_typeinfo->typeinfo->GetDocumentation(
      pvardesc->memid, name.out(), NULL, NULL, NULL));
    CHK_LCOM_ERR(name.get(), "Variable name is unavailable.");

    lua_pushstring(L, tUtil::bstr2string(name.get()));
  }
  lua_settable(L, -3);

  // if it is a constant, stores it's value
  if(pvardesc->varkind == VAR_CONST)
  {
    lua_pushstring(L, "value");
  
    lua_typeinfo->typehandler->com2lua(L, *pvardesc->lpvarValue);

    // sets in the table
    lua_settable(L, -3);
  }
    
  LUASTACK_CLEAN(L, 1);

  return 1;
}

int tLuaTypeInfo::GetImplType(tLuaObject* lua_obj, lua_State* L)
{
  LUASTACK_SET(L);

  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);
  
  UINT i = (UINT)lua_tointeger(L, 2);

  // gets implemented type
  tCOMPtr<ITypeInfo> ptinfo;
  HREFTYPE hreftype;
  CHK_COM_CODE(lua_typeinfo->typeinfo->GetRefTypeOfImplType(i, &hreftype));
  CHK_COM_CODE(lua_typeinfo->typeinfo->GetRefTypeInfo(hreftype, &ptinfo));

  tLuaTypeInfo::pushNew(L, ptinfo);
  LUASTACK_CLEAN(L, 1);
  return 1;
}

int tLuaTypeInfo::GetImplTypeFlags(tLuaObject* lua_obj, lua_State* L)
{
  LUASTACK_SET(L);

  HRESULT hr = S_OK;

  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  int typeflags = 0;
  UINT i = (UINT)lua_tointeger(L, 2);

  CHK_COM_CODE(lua_typeinfo->typeinfo->GetImplTypeFlags(i, &typeflags));

  lua_newtable(L);

  lua_pushstring(L, "default");
  lua_pushboolean(L, typeflags & IMPLTYPEFLAG_FDEFAULT);
  lua_settable(L, -3);

  lua_pushstring(L, "source");
  lua_pushboolean(L, typeflags & IMPLTYPEFLAG_FSOURCE);
  lua_settable(L, -3);

  lua_pushstring(L, "restricted");
  lua_pushboolean(L, typeflags & IMPLTYPEFLAG_FRESTRICTED);
  lua_settable(L, -3);

  lua_pushstring(L, "defaultvtable");
  lua_pushboolean(L, typeflags & IMPLTYPEFLAG_FDEFAULTVTABLE);
  lua_settable(L, -3);

  LUASTACK_CLEAN(L, 1);

  return 1;
}



int tLuaTypeInfo::GetDocumentation(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  tBstrHolder name;
  tBstrHolder helpstring;
  ULONG helpcontext = 0;
  tBstrHolder helpfile;
  CHK_COM_CODE(lua_typeinfo->typeinfo->GetDocumentation(MEMBERID_NIL, 
    name.out(), helpstring.out(), &helpcontext, helpfile.out()));

  tStringBuffer name_text(tUtil::bstr2string(name.get()));
  tStringBuffer help_text(tUtil::bstr2string(helpstring.get()));
  tStringBuffer help_file_text(tUtil::bstr2string(helpfile.get()));

  lua_newtable(L);

  lua_pushstring(L, "name");
  lua_pushstring(L, name_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpstring");
  lua_pushstring(L, help_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpfile");
  lua_pushstring(L, help_file_text);
  lua_settable(L, -3);

  lua_pushstring(L, "helpcontext");
  lua_pushnumber(L, helpcontext);
  lua_settable(L, -3);
 
  return 1;
}

int tLuaTypeInfo::GetTypeAttr(tLuaObject* lua_obj, lua_State* L)
{
  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  TYPEATTR* ptypeattr = NULL;
  HRESULT hr = lua_typeinfo->typeinfo->GetTypeAttr(&ptypeattr);
  tTypeAttrHolder typeattr_holder(lua_typeinfo->typeinfo, ptypeattr);
  CHK_COM_CODE(hr);
  CHK_LCOM_ERR(ptypeattr, "Type attributes are unavailable.");

  // creates table to hold type attributes
  lua_newtable(L);

  // stores guid
  {
    char *pGuid = NULL;

    HRESULT hr = tCOMUtil::GUID2String(ptypeattr->guid, &pGuid);
    if(SUCCEEDED(hr))
    {
      lua_pushstring(L, "GUID");
      lua_pushstring(L, pGuid);
      lua_settable(L, -3);
      delete[] pGuid;
    }
  }

  // stores typekind
  lua_pushstring(L, "typekind");
  lua_pushstring(L, tCOMUtil::getPrintableTypeKind(ptypeattr->typekind));
  lua_settable(L, -3);

  // stores numbers
  lua_pushstring(L, "Funcs");
  lua_pushnumber(L, ptypeattr->cFuncs);
  lua_settable(L, -3);

  lua_pushstring(L, "Vars");
  lua_pushnumber(L, ptypeattr->cVars);
  lua_settable(L, -3);

  lua_pushstring(L, "ImplTypes");
  lua_pushnumber(L, ptypeattr->cImplTypes);
  lua_settable(L, -3);


  // stores flags in a separate table
  lua_newtable(L);

  lua_pushstring(L, "flags");
  lua_pushvalue(L, -2);
  lua_settable(L, -4);

  lua_pushstring(L, "hidden");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FHIDDEN);
  lua_settable(L, -3);

  lua_pushstring(L, "control");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FCONTROL);
  lua_settable(L, -3);

  lua_pushstring(L, "appobject");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FAPPOBJECT);
  lua_settable(L, -3);

  lua_pushstring(L, "dispatchable");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FDISPATCHABLE);
  lua_settable(L, -3);

  lua_pushstring(L, "oleautomation");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FOLEAUTOMATION);
  lua_settable(L, -3);

  lua_pushstring(L, "cancreate");
  lua_pushboolean(L, ptypeattr->wTypeFlags & TYPEFLAG_FCANCREATE);
  lua_settable(L, -3);


  // removes attributes table
  lua_pop(L, 1);

  return 1;
}

int tLuaTypeInfo::GetTypeLib(tLuaObject* lua_obj, lua_State* L)
{
  LUASTACK_SET(L);

  tLuaTypeInfo* lua_typeinfo = DYNAMIC_CAST<tLuaTypeInfo*>(lua_obj);
  CHECKPRECOND(lua_typeinfo);

  // stores a reference to the type library
  tCOMPtr<ITypeLib> typelib;
  unsigned int dumb = 0;
  HRESULT hr = lua_typeinfo->typeinfo->GetContainingTypeLib(&typelib, &dumb);

  if(SUCCEEDED(hr))
  {
    tLuaTLB::pushNew(L, typelib);
  }

  LUASTACK_CLEAN(L, 1);

  return 1;
}
