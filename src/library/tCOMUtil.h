// tCOMUtil.h: interface for the tCOMUtil class.
//
//////////////////////////////////////////////////////////////////////

#ifndef __TCOMUTIL_H
#define __TCOMUTIL_H

#include <windows.h>
#include <oleauto.h>
#include <vector>
#include "tStringBuffer.h"

// The ITypeInfo that supplied the attributes must outlive this holder.
class tTypeAttrHolder
{
public:
  explicit tTypeAttrHolder(ITypeInfo *owner) : m_owner(owner), m_value(NULL) {}
  ~tTypeAttrHolder() { if(m_value) m_owner->ReleaseTypeAttr(m_value); }
  TYPEATTR **out() { return &m_value; }
  TYPEATTR *get() const { return m_value; }

private:
  tTypeAttrHolder(const tTypeAttrHolder&) = delete;
  tTypeAttrHolder& operator=(const tTypeAttrHolder&) = delete;
  ITypeInfo *m_owner;
  TYPEATTR *m_value;
};

// GetNames and GetFieldNames can fill only part of the caller's array.
class tBstrArray
{
public:
  explicit tBstrArray(size_t count = 0) : m_values(count) {}
  ~tBstrArray()
  {
    for(BSTR value : m_values)
      SysFreeString(value);
  }

  BSTR *data() { return m_values.data(); }
  BSTR operator[](size_t index) const { return m_values[index]; }

  void resize(size_t count)
  {
    for(size_t i = count; i < m_values.size(); ++i)
      SysFreeString(m_values[i]);
    m_values.resize(count);
  }

private:
  tBstrArray(const tBstrArray&) = delete;
  tBstrArray& operator=(const tBstrArray&) = delete;
  std::vector<BSTR> m_values;
};

class tCOMUtil  
{
public:
  static HRESULT GUID2String(GUID& Guid, char** ppGuid);
  static const char* getPrintableTypeKind(const TYPEKIND tkind);
  static const char* getPrintableInvokeKind(const INVOKEKIND invkind);
  static tStringBuffer getPrintableTypeDesc(const TYPEDESC& tdesc);
  static void DumpTypeInfo(ITypeInfo* typeinfo);
    static bool GetRegKeyValue(const char* key, char** pValue);
  static bool SetRegKeyValue(const char* key, const char* subkey, const char* value);
    static bool DelRegKey(const char *key, const char *subkey);
  static CLSID GetCLSID(ITypeInfo* coclassinfo);
  static HRESULT ProgID2CLSID(CLSID* pClsid, const char* ProgID);
  static ITypeLib* LoadTypeLibFromCLSID(
    CLSID clsid,
    unsigned short major_version=0);

  static ITypeLib* LoadTypeLibFromProgID(
    const char* ProgID,
    unsigned short major_version=0);

  static ITypeLib* LoadTypeLibByName(const char *pcFilename);

  static CLSID FindCLSID(ITypeInfo* interface_typeinfo);

  static ITypeInfo* GetCoClassTypeInfo(ITypeLib* typelib, CLSID clsid);
  static ITypeInfo* GetCoClassTypeInfo(CLSID clsid);
  static ITypeInfo* GetCoClassTypeInfo(IUnknown* punk);
  static ITypeInfo* GetCoClassTypeInfo(IDispatch* pdisp, CLSID clsid);
  static ITypeInfo* GetCoClassTypeInfo(ITypeLib *typelib,
                                       const char *coclassname);
  static ITypeInfo* GetDefaultInterfaceTypeInfo(ITypeInfo* pCoClassinfo,
                                                bool source);
  static ITypeInfo* GetInterfaceTypeInfo(ITypeLib* typelib,
                                         const char *interface_name);
  static ITypeInfo* GetDispatchTypeInfo(IDispatch* pdisp);

  tCOMUtil();
  virtual ~tCOMUtil();

protected:
  static bool GetDefaultTypeLibVersion(
    const char* libid,
    int* version_major,
    int* version_minor);
};

//#define COM_PRINTREF(x) printf("%p: %d\n", (x), (x)->AddRef()-1); (x)->Release();
#define COM_PRINTREF(x)

#define COM_RELEASE(x) {if(x){(x)->Release(); (x) = NULL;}}

/**
 Smart pointer for COM objects.
 Calls Release() on destruction.
 Has some similarities to CComPtr (atlcomcli.h) in ATL but does not require ATL.
*/
template <class T>
class tCOMPtr
{
public:
  tCOMPtr() : m_p(NULL) { }
  tCOMPtr(const tCOMPtr & o) : m_p(o.m_p) { AddRef();};
  tCOMPtr(T * p) : m_p(p) { AddRef();};
  void operator=(const tCOMPtr<T> & o)
    {
    if (m_p == o.m_p)
      return;
    T * p = o.m_p;
    if (p)
      p->AddRef();
    if (m_p)
      m_p->Release();
    m_p = p;
    }
  ~tCOMPtr() { if (m_p) m_p->Release(); }
  operator T* () const { return m_p; }
  T& operator * () const { return *m_p; }
  T* operator -> () const { return m_p; }
  T** operator & () { return &m_p; }   // useful for QueryInterface
  void Attach(T* p) { if (m_p) m_p->Release(); m_p = p; }
  void Release() { if (m_p) m_p->Release(); m_p = NULL; }
  T * Detach() { T * p = m_p; m_p = NULL; return p; }
  T * Raw() { return m_p; }
private:
  void AddRef() { if (m_p) m_p->AddRef(); }
  T * m_p;
};


#endif // __TCOMUTIL_H
