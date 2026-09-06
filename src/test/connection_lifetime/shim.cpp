#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <stdexcept>
#include <vector>

using HRESULT = int;
using DWORD = unsigned long;
using ULONG = unsigned long;
using UINT = unsigned int;
using DISPID = int;
using IID = int;
const HRESULT S_OK = 0, E_FAIL = -1, CONNECT_E_NOCONNECTION = -2;
const HRESULT DISP_E_EXCEPTION = -3;
const IID IID_NULL = 0, IID_IConnectionPointContainer = 1;
const int LOCALE_SYSTEM_DEFAULT = 0, INVOKE_FUNC = 1, VT_VOID = 24;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define ZeroMemory(p,n) std::memset((p),0,(n))
#define UNUSED(x) (void)(x)
struct tLuaCOMException : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHK_COM_CODE(x) do { HRESULT checked_hr = (x); if(FAILED(checked_hr)) throw tLuaCOMException("COM error"); } while(0)
#define LUACOM_ERROR(x) throw tLuaCOMException(x)
#define COM_ERROR(x) throw tLuaCOMException(x)
#define COM_EXCEPTION(x) throw tLuaCOMException(x)
struct lua_State {};
struct tLuaObjList {};
struct FUNCDESC { struct { struct { int vt; } tdesc; } elemdescFunc; };
struct DISPPARAMS {};
struct VARIANTARG { int vt; };
void VariantInit(VARIANTARG* v) { v->vt = 0; }
void VariantClear(VARIANTARG*) {}
struct EXCEPINFO {
  char *bstrSource, *bstrDescription, *bstrHelpFile;
  unsigned short wCode;
  HRESULT scode;
  HRESULT (*pfnDeferredFillIn)(EXCEPINFO*);
};
struct ExceptionInfoStrings { explicit ExceptionInfoStrings(EXCEPINFO&) {} };
struct tUtil {
  template<class... T> static void log_verbose(T...) {}
  static const char* bstr2string(const char* s) { return s; }
  static const char* GetErrorMessage(int) { return "COM error"; }
};
struct IUnknown {
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;
  virtual ~IUnknown() {}
};
struct IDispatch : IUnknown {
  virtual HRESULT QueryInterface(IID, void**) = 0;
  virtual HRESULT Invoke(DISPID, IID, int, int, DISPPARAMS*, VARIANTARG*, EXCEPINFO*, UINT*) = 0;
};
struct IConnectionPoint : IUnknown {
  virtual HRESULT Advise(IUnknown*, DWORD*) = 0;
  virtual HRESULT Unadvise(DWORD) = 0;
};
struct IConnectionPointContainer : IUnknown {
  virtual HRESULT FindConnectionPoint(IID, IConnectionPoint**) = 0;
};
/* POINTER */

struct FakeDispatch;
struct FakePoint : IConnectionPoint {
  FakeDispatch& owner;
  int refs = 0, advises = 0, unadvises = 0, failures = 0;
  DWORD next_cookie = 400;
  std::map<DWORD,IUnknown*> sinks;
  std::function<void()> during_advise, during_unadvise;
  explicit FakePoint(FakeDispatch& o):owner(o) {}
  ULONG AddRef() override;
  ULONG Release() override;
  HRESULT Advise(IUnknown*, DWORD*) override;
  HRESULT Unadvise(DWORD) override;
};
struct FakeContainer : IConnectionPointContainer {
  FakeDispatch& owner;
  int refs = 0;
  std::function<void()> during_find;
  explicit FakeContainer(FakeDispatch& o):owner(o) {}
  ULONG AddRef() override;
  ULONG Release() override;
  HRESULT FindConnectionPoint(IID, IConnectionPoint**) override;
};
struct FakeDispatch : IDispatch {
  int refs = 1, destroyed = 0, invokes = 0;
  FakePoint point;
  FakeContainer container;
  std::function<void()> during_query, during_invoke, on_final_release;
  FakeDispatch():point(*this),container(*this) {}
  ULONG AddRef() override { assert(refs > 0); return ++refs; }
  ULONG Release() override {
    assert(refs > 0);
    int result = --refs;
    if(!result) { ++destroyed; if(on_final_release) on_final_release(); }
    return result;
  }
  HRESULT QueryInterface(IID iid, void** result) override {
    assert(refs > 0);
    auto cb = during_query; during_query = nullptr; if(cb) cb();
    assert(refs > 0);
    assert(iid == IID_IConnectionPointContainer);
    container.AddRef(); *result = &container; return S_OK;
  }
  HRESULT Invoke(DISPID,IID,int,int,DISPPARAMS*,VARIANTARG*,EXCEPINFO*,UINT*) override {
    assert(refs > 0); ++invokes;
    auto cb = during_invoke; during_invoke = nullptr; if(cb) cb();
    assert(refs > 0); return S_OK;
  }
};
ULONG FakePoint::AddRef() { ++refs; return owner.AddRef(); }
ULONG FakePoint::Release() { assert(refs > 0); --refs; return owner.Release(); }
HRESULT FakePoint::Advise(IUnknown* sink, DWORD* cookie) {
  assert(owner.refs > 0); ++advises;
  sink->AddRef(); DWORD id = next_cookie++; sinks[id] = sink;
  auto cb = during_advise; during_advise = nullptr; if(cb) cb();
  assert(owner.refs > 0); *cookie = id; return S_OK;
}
HRESULT FakePoint::Unadvise(DWORD cookie) {
  assert(owner.refs > 0); ++unadvises;
  auto cb = during_unadvise; during_unadvise = nullptr; if(cb) cb();
  if(failures) { --failures; return E_FAIL; }
  auto it = sinks.find(cookie);
  if(it == sinks.end()) return CONNECT_E_NOCONNECTION;
  IUnknown* sink = it->second; sinks.erase(it); sink->Release(); return S_OK;
}
ULONG FakeContainer::AddRef() { ++refs; return owner.AddRef(); }
ULONG FakeContainer::Release() { assert(refs > 0); --refs; return owner.Release(); }
HRESULT FakeContainer::FindConnectionPoint(IID, IConnectionPoint** result) {
  auto cb = during_find; during_find = nullptr; if(cb) cb();
  assert(owner.refs > 0); owner.point.AddRef(); *result = &owner.point; return S_OK;
}
struct Handler {
  std::function<void()> during_fill, during_result;
  void fillDispParams(lua_State*,DISPPARAMS&,FUNCDESC*,tLuaObjList,int) { if(during_fill) during_fill(); }
  void com2lua(lua_State*,VARIANTARG) { if(during_result) during_result(); }
  int pushOutValues(lua_State*,DISPPARAMS&,FUNCDESC*) { return 0; }
  void releaseVariants(DISPPARAMS*) {}
};
class tLuaCOM {
public:
  struct Connection {
    Connection(IConnectionPoint* p, const IID& id):point(p),interface_id(id),cookie(0) {}
    tCOMPtr<IConnectionPoint> point;
    IID interface_id;
    DWORD cookie;
  };
  using ConnectionList = std::list<Connection>;
  tCOMPtr<IDispatch> pdisp;
  ConnectionList connections;
  IID last_connection_interface=IID_NULL;
  DWORD last_connection_cookie=0;
  Handler handler;
  Handler* typehandler = &handler;
  int ID = 1;
  explicit tLuaCOM(FakeDispatch& d):pdisp(&d) { d.Release(); }
  ~tLuaCOM() { releaseConnections(); }
  bool hasTypeInfo() { return true; }
  IDispatch* GetIDispatch() { checkComObject(); return pdisp; }
  void GetIID(IID* iid) { *iid=7; }
  bool released() { return !pdisp; }
  void checkComObject() const;
  int call(lua_State*,DISPID,int,FUNCDESC*,tLuaObjList);
  DWORD addConnection(tLuaCOM*);
  void releaseConnection();
  void releaseConnection(tLuaCOM*,DWORD);
  HRESULT releaseConnections();
  void releaseComObject();
};
/* FUNCTIONS */

template<class F> void must_throw(F fn) {
  bool thrown = false; try { fn(); } catch(const tLuaCOMException&) { thrown = true; }
  assert(thrown);
}
int tests = 0;
template<class F> void test(const char* name,F fn) { fn(); ++tests; std::cout << "PASS " << name << '\n'; }
