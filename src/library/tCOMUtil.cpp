/**
  tCOMUtil.cpp: implementation of the tCOMUtil class.
*/

#include <stdio.h>
#include <ocidl.h>
#include <shlwapi.h>
#include <wchar.h>
#include <new>
#include <string>

#include "tCOMUtil.h"
#include "tLuaCOMException.h"
#include "tUtil.h"



///
/// Construction/Destruction
///



tCOMUtil::tCOMUtil()
{

}

tCOMUtil::~tCOMUtil()
{

}

ITypeInfo *tCOMUtil::GetCoClassTypeInfo(CLSID clsid)
{
  ITypeLib* typelib = tCOMUtil::LoadTypeLibFromCLSID(clsid);

  if(typelib == NULL)
    return NULL;

  return tCOMUtil::GetCoClassTypeInfo(typelib, clsid);
}

ITypeInfo *tCOMUtil::GetCoClassTypeInfo(IUnknown* punk)
{
  HRESULT hr = S_OK;
  
  tCOMPtr<IProvideClassInfo> pIProvideClassInfo;
  hr = punk->QueryInterface(IID_IProvideClassInfo,
    (void **) &pIProvideClassInfo);
  if (FAILED(hr))
    return NULL;

  ITypeInfo* coclassinfo = NULL;
  hr = pIProvideClassInfo->GetClassInfo(&coclassinfo);
  if(SUCCEEDED(hr))
    return coclassinfo;
  else
    return NULL;
}

ITypeInfo *tCOMUtil::GetCoClassTypeInfo(IDispatch* pdisp, CLSID clsid)
{
  CHECKPARAM(pdisp);

  HRESULT hr = S_OK;

  unsigned int typeinfocount = 0;
  hr = pdisp->GetTypeInfoCount(&typeinfocount);
  if(FAILED(hr) || typeinfocount == 0)
    return NULL;

  tCOMPtr<ITypeInfo> typeinfo;
  hr = pdisp->GetTypeInfo(0, 0, &typeinfo);
  if(FAILED(hr))
    return NULL;

  tCOMPtr<ITypeLib> typelib;
  unsigned int dumb_index = (unsigned int)-1;
  hr = typeinfo->GetContainingTypeLib(&typelib, &dumb_index);
  if(FAILED(hr))
    return NULL;

  ITypeInfo* coclasstypeinfo = tCOMUtil::GetCoClassTypeInfo(typelib, clsid);

  return coclasstypeinfo;
}

ITypeInfo *tCOMUtil::GetCoClassTypeInfo(ITypeLib *typelib,
                                        const char *coclassname)
{
  wchar_t* wcCoClass = (wchar_t*) 
    malloc( (strlen(coclassname) + 1) * sizeof(wchar_t));

  mbstowcs(wcCoClass, coclassname, strlen(coclassname)+1);

  const short max_typeinfos = 1000;

  MEMBERID dumb[max_typeinfos];
  ITypeInfo *typeinfos[max_typeinfos];
  unsigned short number = max_typeinfos;
  
  HRESULT hr = typelib->FindName(wcCoClass, 0, 
    typeinfos, dumb, &number);

  free(wcCoClass);
  wcCoClass = NULL;

  // a interface especificada nao existe nesta TypeLib
  if(FAILED(hr) || number == 0)
    return NULL;

  // tries to find the coclass

  unsigned int i = 0;
  TYPEATTR *typeattr = NULL;
  ITypeInfo *found = NULL;

  for(i = 0; i < number; i++)
  {
    hr = typeinfos[i]->GetTypeAttr(&typeattr);
    if(FAILED(hr) || typeattr == NULL)
    {
      typeinfos[i]->Release();
      continue;
    }
    
    TYPEKIND typekind = typeattr->typekind;
    typeinfos[i]->ReleaseTypeAttr(typeattr);

    if(!found && typekind == TKIND_COCLASS)
      found = typeinfos[i];
    else
      typeinfos[i]->Release();
  }

  return found;
}



ITypeInfo *tCOMUtil::GetDefaultInterfaceTypeInfo(ITypeInfo* pCoClassinfo,
                                                 bool source)
           
{
  ITypeInfo* typeinfo = NULL;

  // if the component does not have a dispinterface typeinfo 
  // for events, we stay with an interface typeinfo
  ITypeInfo* interface_typeinfo = NULL;

  TYPEATTR* pTA = NULL;
  HRESULT hr = S_OK;

  if(pCoClassinfo == NULL)
    return NULL;

  if (SUCCEEDED(pCoClassinfo->GetTypeAttr(&pTA)) && pTA != NULL)
  {
    UINT i = 0;
    int iFlags = 0;

    for (i=0; i < pTA->cImplTypes; i++)
    {
      //Get the implementation type for this interface
      hr = pCoClassinfo->GetImplTypeFlags(i, &iFlags);

      if (FAILED(hr))
        continue;

      if (iFlags & IMPLTYPEFLAG_FDEFAULT || pTA->cImplTypes == 1)
      {
        if(source == false && !(iFlags & IMPLTYPEFLAG_FSOURCE)
        || source == true && (iFlags & IMPLTYPEFLAG_FSOURCE))
        {
          HREFTYPE    hRefType=0;

          /*
           * This is the interface we want.  Get a handle to
           * the type description from which we can then get
           * the ITypeInfo.
           */
          hr = pCoClassinfo->GetRefTypeOfImplType(i, &hRefType);
          if(FAILED(hr))
            continue;

          hr = pCoClassinfo->GetRefTypeInfo(hRefType, &typeinfo);
          if(FAILED(hr) || typeinfo == NULL)
          {
            COM_RELEASE(typeinfo);
            continue;
          }

          // gets typeattr info
          TYPEATTR *ptypeattr = NULL;
          GUID guid;
          TYPEKIND typekind;

          hr = typeinfo->GetTypeAttr(&ptypeattr);

          if(FAILED(hr) || ptypeattr == NULL)
          {
            COM_RELEASE(typeinfo);
            break;
          }

          guid = ptypeattr->guid;
          typekind = ptypeattr->typekind;

          typeinfo->ReleaseTypeAttr(ptypeattr);

          COM_RELEASE(interface_typeinfo);
          if(typekind == TKIND_DISPATCH)
          {
            break;  // found!
          }
          else // hold this pointer. If we do not find
              // anything better, we stay with this typeinfo
          {
            interface_typeinfo = typeinfo;
            typeinfo = NULL;
          }
        }
      }
    }

    pCoClassinfo->ReleaseTypeAttr(pTA);
  }

  if(!typeinfo)
    return interface_typeinfo;
  else
    return typeinfo;
}



ITypeInfo *tCOMUtil::GetDispatchTypeInfo(IDispatch* pdisp)
{
  ITypeInfo* typeinfo = NULL;
  HRESULT hr = pdisp->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &typeinfo);
  if(FAILED(hr) || typeinfo == NULL)
    return NULL;

  TYPEATTR *ptypeattr = NULL;
  hr = typeinfo->GetTypeAttr(&ptypeattr);
  if(FAILED(hr) || ptypeattr == NULL)
  {
    COM_RELEASE(typeinfo);
    return NULL;
  }

  TYPEKIND typekind = ptypeattr->typekind;

  if(typekind == TKIND_DISPATCH)
  {
    typeinfo->ReleaseTypeAttr(ptypeattr);
    return typeinfo;
  }

  // tries to find another description of the same
  // interface in the typelib with TKIND_DISPATCH

  IID iid = ptypeattr->guid;
  
  tCOMPtr<ITypeLib> ptypelib;
  unsigned int dumb = 0;
  hr = typeinfo->GetContainingTypeLib(&ptypelib, &dumb);

  typeinfo->ReleaseTypeAttr(ptypeattr);

  // if there's no containing type lib, we have to
  // trust this one is the right type info
  if(FAILED(hr) || !ptypelib)
    return typeinfo;

  // obtem a typeinfo do iid fornecido
  // caso haja uma implementacao dispinterface,
  // esta' e' que sera' retornada (segundo
  // documentacao do ActiveX

  ITypeInfo* typeinfo_guid = NULL;
  hr = ptypelib->GetTypeInfoOfGuid(iid, &typeinfo_guid);
  if(FAILED(hr) || typeinfo_guid == NULL)
    return typeinfo;

  // verifica se e' dispinterface
  TYPEATTR *ptypeattr_iface = NULL;
  hr = typeinfo_guid->GetTypeAttr(&ptypeattr_iface);
  if(FAILED(hr) || ptypeattr_iface == NULL)
  {
    COM_RELEASE(typeinfo_guid);
    return typeinfo;
  }
  TYPEKIND typekind_iface = ptypeattr_iface->typekind;
  typeinfo_guid->ReleaseTypeAttr(ptypeattr_iface);

  if(typekind_iface == TKIND_DISPATCH)
  {
    // releases original type information
    COM_RELEASE(typeinfo);

    return typeinfo_guid;
  }
  else
  {
    COM_RELEASE(typeinfo_guid);
    
    // returns original type info
    return typeinfo;
  }
}


ITypeInfo *tCOMUtil::GetInterfaceTypeInfo(ITypeLib* typelib,
                                          const char *interface_name)
{
  wchar_t* wcInterface = (wchar_t*) 
      malloc( (strlen(interface_name) + 1) * sizeof(wchar_t));
  mbstowcs(wcInterface, interface_name, strlen(interface_name)+1);

  const int max_typeinfos = 30;

  MEMBERID dumb[max_typeinfos];
  ITypeInfo *typeinfos[max_typeinfos];
  unsigned int number = 30;
  
  HRESULT hr = typelib->FindName(wcInterface, 0, 
    typeinfos, dumb, (unsigned short *) &number);

  free(wcInterface);
  wcInterface = NULL;

  // a interface especificada nao existe nesta TypeLib
  if(FAILED(hr) || number == 0)
    return NULL;

  // Procura por uma implementacao IDispatch
  
  unsigned int i = 0;
  TYPEATTR *typeattr = NULL;
  ITypeInfo *found = NULL;

  for(i = 0; i < number; i++)
  {
    hr = typeinfos[i]->GetTypeAttr(&typeattr);
    if(FAILED(hr) || typeattr == NULL)
    {
      typeinfos[i]->Release();
      continue;
    }
    
    TYPEKIND typekind = typeattr->typekind;
    typeinfos[i]->ReleaseTypeAttr(typeattr);

    if(!found && typekind == TKIND_DISPATCH)
      found = typeinfos[i];
    else
      typeinfos[i]->Release();
  }

  return found;
}


/**
  Carrega uma Typelib associada a um ProgID
*/
ITypeLib* tCOMUtil::LoadTypeLibFromProgID(const char* ProgID,
                                          unsigned short major_version)
{
  CLSID clsid = IID_NULL;
  HRESULT hr = tCOMUtil::ProgID2CLSID(&clsid, ProgID);

  if(FAILED(hr))
    return NULL;

  bool version_found = false;

  if(major_version == 0)
  {
    if(sscanf(ProgID, "%*s.%*s.%hd", &major_version) == 1)
      version_found = true;
  }

  // tries to get some version information to help finding
  // the right type library
  if(version_found)
    return tCOMUtil::LoadTypeLibFromCLSID(clsid, major_version);
  else
    return tCOMUtil::LoadTypeLibFromCLSID(clsid);

}


/**
  LoadTypeLibByName
  Carrega uma typelib a partir de um arquivo TLB
*/
ITypeLib *tCOMUtil::LoadTypeLibByName(const char *pcFilename)
{
  HRESULT hr;
  ITypeLib *ptlib = NULL;
  wchar_t *wcFilename = new wchar_t[strlen(pcFilename)+1];

  mbstowcs(wcFilename, pcFilename, strlen(pcFilename)+1);

  hr = LoadTypeLibEx(wcFilename, REGKIND_NONE, &ptlib);

  delete[] wcFilename;
  wcFilename = NULL;

  if(FAILED(hr))
    return NULL;

  return ptlib;
}



ITypeLib* tCOMUtil::LoadTypeLibFromCLSID(CLSID clsid,
                                         unsigned short major_version)
{
  wchar_t* wcClsid = NULL;

  HRESULT hr = StringFromCLSID(clsid, &wcClsid);

  if (FAILED(hr))
    return NULL;

  /* converte CLSID para string normal */
  char* pcClsid = (char*) malloc( (wcslen(wcClsid) + 1) * sizeof(char));
  if(pcClsid == NULL)
  {
    CoTaskMemFree(wcClsid);
    return NULL;
  }
  wcstombs(pcClsid, wcClsid,wcslen(wcClsid)+1);

  CoTaskMemFree(wcClsid);

  const DWORD libid_capacity = 38*3; /*{F37C8063-4AD5-101B-B826-00DD01103DE1}*/
  BYTE *bLibID = (BYTE *) calloc(libid_capacity + 1, 1);
  const DWORD version_capacity = 100;
  BYTE bVersion[version_capacity + 1] = { 0 }; // This must hold something like "5.2"
  HKEY iid_key = NULL, obj_key = NULL, typelib_key = NULL, version_key = NULL;

  /* extrai do registry type library (GUID e versao) */
  LONG res = 0;
  bool version_info_found = true;

  if(bLibID == NULL)
  {
    free(pcClsid);
    return NULL;
  }

  try
  {
    res = RegOpenKeyExA(HKEY_CLASSES_ROOT,"CLSID", 0, KEY_READ, &iid_key);
    WINCHECK(res == ERROR_SUCCESS);

    res = RegOpenKeyExA(iid_key, pcClsid, 0, KEY_READ, &obj_key);
    RegCloseKey(iid_key);
    iid_key = NULL;
    free(pcClsid);
    pcClsid = NULL;

    WINCHECK(res == ERROR_SUCCESS);

    res = RegOpenKeyExA(obj_key, "TypeLib",0, KEY_READ, &typelib_key);
    if(res != ERROR_SUCCESS)
    {
      RegCloseKey(obj_key);
      obj_key = NULL;
      LUACOM_EXCEPTION(WINDOWS_ERROR);
    }

    res = RegOpenKeyExA(obj_key, "version",0, KEY_READ, &version_key);
    RegCloseKey(obj_key);
    obj_key = NULL;
    if(res != ERROR_SUCCESS)
      version_info_found = false;

    DWORD libid_type = 0;
    DWORD libid_size = libid_capacity;
    res = RegQueryValueExA(typelib_key, NULL, NULL, &libid_type,
                           bLibID, &libid_size);
    RegCloseKey(typelib_key);
    typelib_key = NULL;
    WINCHECK(res == ERROR_SUCCESS && libid_type == REG_SZ &&
             libid_size > 1 && libid_size <= libid_capacity);
    bLibID[libid_size] = '\0';

    if(version_info_found)
    {
      DWORD version_type = 0;
      DWORD version_size = version_capacity;
      res = RegQueryValueExA(version_key, NULL, NULL, &version_type,
                             bVersion, &version_size);
      RegCloseKey(version_key);
      version_key = NULL;
      if(res != ERROR_SUCCESS || version_type != REG_SZ ||
         version_size <= 1 || version_size > version_capacity)
        version_info_found = false;
      else
        bVersion[version_size] = '\0';
    }
  }
  catch(class tLuaCOMException& e)
  {
    UNUSED(e);

    if(pcClsid)
      free(pcClsid);

    if(iid_key)
      RegCloseKey(iid_key);
    if(obj_key)
      RegCloseKey(obj_key);
    if(typelib_key)
      RegCloseKey(typelib_key);
    if(version_key)
      RegCloseKey(version_key);

    free(bLibID);
    return NULL;
  }

  // converts libID to multibyte string
  wchar_t* wcTypelib= (wchar_t*) 
    malloc( (strlen((char *) bLibID) + 1) * sizeof(wchar_t));
  if(wcTypelib == NULL)
  {
    free(bLibID);
    return NULL;
  }
  mbstowcs(wcTypelib, (char *) bLibID, strlen((char *) bLibID)+1);

  // extracts version information

  int version_major = 0, version_minor = 0;

  int found = 0;

  if(version_info_found)
  {
    found = sscanf(
      (const char *) bVersion,
      "%d.%d",
      &version_major, &version_minor);

    if(found != 2)
      version_info_found = false;
  }

  if(major_version > 0 && !version_info_found)
  {
    version_major = major_version;
    version_minor = 0;
  }
  else if(!version_info_found)
  {
    // tries to load the first type library found in
    // the registry
    bool result = tCOMUtil::GetDefaultTypeLibVersion(
      (const char*) bLibID,
      &version_major,
      &version_minor);

    if(!result) {
      free(bLibID);
      free(wcTypelib);
      return NULL;
    }
  }

  free(bLibID);

  GUID libid = IID_NULL;
  hr = CLSIDFromString(wcTypelib, &libid);
  free(wcTypelib);

  if(FAILED(hr))
    return NULL;

  ITypeLib* typelib = NULL;

  hr = LoadRegTypeLib(libid, version_major, version_minor, 0, &typelib);

  if(FAILED(hr) || typelib == NULL)
    return NULL;

  return typelib;
}

ITypeInfo* tCOMUtil::GetCoClassTypeInfo(ITypeLib *typelib, CLSID clsid)
{
  ITypeInfo* coclassinfo = NULL;
  
  HRESULT hr = typelib->GetTypeInfoOfGuid(clsid, &coclassinfo);
  
  if(FAILED(hr))
    return NULL;

  return coclassinfo;
}

HRESULT tCOMUtil::ProgID2CLSID(CLSID *pClsid, const char *ProgID)
{
  CHECKPARAM(pClsid);
  
  // tests whether we already have a CLSID

  wchar_t* wcProgId = NULL;
  HRESULT hr = S_OK;

  if(ProgID[0] == '{')
  {
    wcProgId = (wchar_t*) malloc( (strlen(ProgID) + 1) * sizeof(wchar_t));
    mbstowcs(wcProgId, ProgID, strlen(ProgID)+1);

    hr = CLSIDFromString(wcProgId, pClsid);

    free(wcProgId);
    wcProgId = NULL;

    return hr;
  }

  /* converte ProgID para OLESTR */
  wcProgId = (wchar_t*) malloc( (strlen(ProgID) + 1) * sizeof(wchar_t));
  mbstowcs(wcProgId, ProgID, strlen(ProgID)+1);

  hr = CLSIDFromProgID(wcProgId, pClsid);

  free(wcProgId);
  wcProgId = NULL;

  return hr;
}

CLSID tCOMUtil::GetCLSID(ITypeInfo *coclassinfo)
{
  TYPEATTR* ptypeattr = NULL;

  HRESULT hr = coclassinfo->GetTypeAttr(&ptypeattr);

  if(FAILED(hr) || ptypeattr == NULL)
    return IID_NULL;

  CLSID clsid = ptypeattr->guid;

  coclassinfo->ReleaseTypeAttr(ptypeattr);

  return clsid;
}

bool tCOMUtil::GetDefaultTypeLibVersion(const char* libid,
                                        int *version_major,
                                        int *version_minor)
{
  LONG res = 0;
  HKEY typelib_key, this_typelib_key;

  res = RegOpenKeyExA(HKEY_CLASSES_ROOT,"TypeLib", 0, KEY_READ, &typelib_key);
  if(res != ERROR_SUCCESS)
    return false;

  res = RegOpenKeyExA(typelib_key, libid, 0, KEY_READ, &this_typelib_key);
  RegCloseKey(typelib_key);

  if(res != ERROR_SUCCESS)
    return false;

  const int bufsize = 1000;
  char version_info[bufsize + 1] = { 0 };
  DWORD size = bufsize;

  res = RegEnumKeyExA(this_typelib_key, 0, version_info, &size,
    NULL, NULL, NULL, NULL); 
  RegCloseKey(this_typelib_key);

  if(res != ERROR_SUCCESS)
    return false;

  version_info[size] = '\0';
  return sscanf(version_info, "%d.%d", version_major, version_minor) == 2;
}

bool tCOMUtil::GetRegKeyValue(const char* key, char** pValue) {
  LONG ec = 0;
  LONG cbValue;

  ec = RegQueryValueA(HKEY_CLASSES_ROOT,key,NULL,&cbValue);

  if(ERROR_SUCCESS == ec) {
    *pValue = new char[cbValue+1];
    ec = RegQueryValueA(HKEY_CLASSES_ROOT,key,*pValue,&cbValue);
    if(ERROR_SUCCESS == ec)
    {
      (*pValue)[cbValue] = '\0';
      return true;
    }

    delete[] *pValue;
    *pValue = NULL;
  }

  return false;
}

bool tCOMUtil::SetRegKeyValue(const char *key,
                              const char *subkey,
                              const char *value)
{

  bool ok = false;
  LONG ec = 0;
  HKEY hKey;

  std::string Key = key;

  if (NULL != subkey)
  {
    Key += "\\";
    Key += subkey;
  }

  ec = RegCreateKeyExA(
         HKEY_CLASSES_ROOT,
         Key.c_str(),
         0,
         NULL,
         REG_OPTION_NON_VOLATILE,
         KEY_ALL_ACCESS,
         NULL,
         &hKey,
         NULL);

  if (ERROR_SUCCESS == ec)
  {
    if (NULL != value)
    {
      ec = RegSetValueExA(
             hKey,
             NULL,
             0,
             REG_SZ,
             (BYTE *)value,
             (DWORD)(strlen(value)+1));
    }
    if (ERROR_SUCCESS == ec)
      ok = TRUE;

    RegCloseKey(hKey);
  }

  return ok;
}

bool tCOMUtil::DelRegKey(const char *key,
                         const char *subkey)
{
  std::string Key = key;

  if (NULL != subkey)
  {
    Key += "\\";
    Key += subkey;
  }

  LONG ec = SHDeleteKeyA(HKEY_CLASSES_ROOT, Key.c_str());
  if (ERROR_SUCCESS == ec || ERROR_FILE_NOT_FOUND == ec ||
      ERROR_PATH_NOT_FOUND == ec)
  {
    return true;
  }

  return false;
}

void tCOMUtil::DumpTypeInfo(ITypeInfo *typeinfo)
{
  HRESULT hr = S_OK;
  TYPEATTR* pta = NULL;

  CHECKPARAM(typeinfo);

  CHK_COM_CODE(typeinfo->GetTypeAttr(&pta));

  // prints IID
  LPOLESTR lpsz = NULL;

#ifdef __WINE__
  hr = 0;
  MessageBoxA(NULL, "FIX - not implemented - StringFromIID", "LuaCOM", MB_ICONEXCLAMATION);
  #warning FIX - not implemented - StringFromIID
#else
  hr = StringFromIID(pta->guid, &lpsz);
#endif

  if(FAILED(hr))
  {
    hr = StringFromCLSID(pta->guid, &lpsz);
  }

  if(SUCCEEDED(hr))
  {
    wprintf(L"\nInterface:  %s\n\n", lpsz);

    CoTaskMemFree(lpsz);
  }

  for(int i = 0; i < pta->cFuncs; i++)
  {
    FUNCDESC *pfd = NULL;
    hr = typeinfo->GetFuncDesc(i, &pfd);
    if(FAILED(hr) || pfd == NULL)
      continue;

    BSTR names[1] = { NULL };
    unsigned int dumb = 0;
    hr = typeinfo->GetNames(pfd->memid, names, 1, &dumb);

    if(FAILED(hr) || dumb == 0 || names[0] == NULL)
    {
      SysFreeString(names[0]);
      typeinfo->ReleaseFuncDesc(pfd);
      continue;
    }

    tStringBuffer function_name;
    try
    {
      function_name = tUtil::bstr2string(names[0]);
    }
    catch (...)
    {
      SysFreeString(names[0]);
      typeinfo->ReleaseFuncDesc(pfd);
      typeinfo->ReleaseTypeAttr(pta);
      throw;
    }

    printf("%.3d: %-30s\tid=0x%lx\t%d param(s)\n", i,
      function_name.getBuffer(), (unsigned long)pfd->memid, pfd->cParams);

    typeinfo->ReleaseFuncDesc(pfd);
    SysFreeString(names[0]);
  }

  typeinfo->ReleaseTypeAttr(pta);
}


const char* tCOMUtil::getPrintableInvokeKind(const INVOKEKIND invkind)
{
  switch(invkind)
  {
  case INVOKE_PROPERTYGET:
    return "propget";

  case INVOKE_PROPERTYPUT:
    return "propput";

  case INVOKE_PROPERTYPUTREF:
    return "propputref";

  case INVOKE_FUNC:
    return "func";
  }

  return NULL;
}

tStringBuffer tCOMUtil::getPrintableTypeDesc(const TYPEDESC& tdesc)
{
  char buffer[200];
  buffer[0] = '\0';

  switch(tdesc.vt & ~(VT_ARRAY | VT_BYREF))
  {
  case VT_VOID:
    strcat(buffer, "void");
    break;

  case VT_I2:
    strcat(buffer, "short");
    break;

  case VT_I4:
    strcat(buffer, "long");
    break;

  case VT_R4:
    strcat(buffer, "float");
    break;

  case VT_R8:
    strcat(buffer, "double");
    break;

  case VT_CY:
    strcat(buffer, "CY");
    break;

  case VT_DATE:
    strcat(buffer, "DATE");
    break;

  case VT_BSTR:
    strcat(buffer, "BSTR");
    break;

  case VT_DISPATCH:
    strcat(buffer, "IDispatch*");
    break;

  case VT_BOOL:
    strcat(buffer, "VARIANT_BOOL");
    break;

  case VT_VARIANT:
    strcat(buffer, "VARIANT");
    break;

  case VT_UNKNOWN:
    strcat(buffer, "IUnknown*");
    break;

  case VT_DECIMAL:
    strcat(buffer, "Decimal");
    break;

  case VT_UI1:
    strcat(buffer, "unsigned char");
    break;

  case VT_INT:
    strcat(buffer, "int");
    break;

  case VT_HRESULT:
    strcat(buffer, "void");
    break;
  }

  if(tdesc.vt & VT_BYREF)
    strcat(buffer, "*");

  if(tdesc.vt & VT_ARRAY)
    strcat(buffer, "[]");

  return tStringBuffer(buffer);
}

const char* tCOMUtil::getPrintableTypeKind(const TYPEKIND tkind)
{
  switch(tkind)
  {
  case TKIND_COCLASS:
    return "coclass";
    break;

  case TKIND_ENUM:
    return "enumeration";
    break;

  case TKIND_RECORD:
    return "record";
    break;

  case TKIND_MODULE:
    return "module";
    break;

  case TKIND_INTERFACE:
    return "interface";
    break;

  case TKIND_DISPATCH:
    return "dispinterface";
    break;

  case TKIND_ALIAS:
    return "alias";
    break;

  case TKIND_UNION:
    return "union";
    break;

  default:
    return "";
    break;
  }
}


HRESULT tCOMUtil::GUID2String(GUID& Guid, char** ppGuid)
{
  if(!ppGuid)
    return E_POINTER;

  *ppGuid = NULL;
  wchar_t* wcGuid = NULL;

  HRESULT hr = StringFromCLSID(Guid, &wcGuid);

  if (FAILED(hr))
    return hr;
  if(!wcGuid)
    return E_UNEXPECTED;

  int length = WideCharToMultiByte(CP_ACP, 0, wcGuid, -1,
                                   NULL, 0, NULL, NULL);
  if(length <= 0)
  {
    DWORD error = GetLastError();
    CoTaskMemFree(wcGuid);
    return HRESULT_FROM_WIN32(error ? error : ERROR_NO_UNICODE_TRANSLATION);
  }

  char * result = NULL;
  try
  {
    result = new char[length];
  }
  catch(const std::bad_alloc&)
  {
    CoTaskMemFree(wcGuid);
    return E_OUTOFMEMORY;
  }

  if(WideCharToMultiByte(CP_ACP, 0, wcGuid, -1, result,
                         length, NULL, NULL) != length)
  {
    DWORD error = GetLastError();
    delete[] result;
    CoTaskMemFree(wcGuid);
    return HRESULT_FROM_WIN32(error ? error : ERROR_NO_UNICODE_TRANSLATION);
  }

  CoTaskMemFree(wcGuid);

  *ppGuid = result;

  return S_OK;
}

CLSID tCOMUtil::FindCLSID(ITypeInfo* interface_typeinfo)
{
  if(interface_typeinfo == NULL)
    return IID_NULL;

  // gets IID
  TYPEATTR* ptypeattr = NULL;
  HRESULT hr = interface_typeinfo->GetTypeAttr(&ptypeattr);
  if(FAILED(hr) || ptypeattr == NULL)
    return IID_NULL;
  IID iid = ptypeattr->guid;
  interface_typeinfo->ReleaseTypeAttr(ptypeattr);
  ptypeattr = NULL;

  // Gets type library
  tCOMPtr<ITypeLib> ptypelib;
  UINT typelib_index = 0;
  hr = interface_typeinfo->GetContainingTypeLib(&ptypelib, &typelib_index);
  if(FAILED(hr) || !ptypelib)
    return IID_NULL;

  // iterates looking for IID inside some coclass
  long count = ptypelib->GetTypeInfoCount();
  bool found = false;
  CLSID clsid = IID_NULL;
  while(count-- && !found)
  {
    TYPEKIND tkind;
    hr = ptypelib->GetTypeInfoType(count, &tkind);
    if(FAILED(hr))
      continue;

    if(tkind != TKIND_COCLASS)
      continue;

    // look inside
    tCOMPtr<ITypeInfo> ptypeinfo;
    hr = ptypelib->GetTypeInfo(count, &ptypeinfo);
    if(FAILED(hr) || !ptypeinfo)
      continue;

    // gets counts and clsid
    TYPEATTR* ptypeattr = NULL;
    hr = ptypeinfo->GetTypeAttr(&ptypeattr);
    if(FAILED(hr) || ptypeattr == NULL)
      continue;
    long ifaces_count   = ptypeattr->cImplTypes;
    clsid = ptypeattr->guid;
    ptypeinfo->ReleaseTypeAttr(ptypeattr);
    ptypeattr = NULL;

    while(ifaces_count-- && !found)
    {
      HREFTYPE RefType;
      hr = ptypeinfo->GetRefTypeOfImplType(ifaces_count, &RefType);
      if(FAILED(hr))
        continue;
      tCOMPtr<ITypeInfo> piface_typeinfo;
      hr = ptypeinfo->GetRefTypeInfo(RefType, &piface_typeinfo);
      if(FAILED(hr) || !piface_typeinfo)
        continue;
      hr = piface_typeinfo->GetTypeAttr(&ptypeattr);
      if(FAILED(hr) || ptypeattr == NULL)
        continue;

      if(IsEqualIID(ptypeattr->guid, iid))
      {
        found = true;
      }

      piface_typeinfo->ReleaseTypeAttr(ptypeattr);
      ptypeattr = NULL;
    }
  }

  return found ? clsid : IID_NULL;
}
