/**
  tLuaCOMConnPoints.cpp - COM connection points.
  
  WARNING: This code has been significantly refactored but largely untested
  because I don't use this feature, I don't know who does, and test cases are lacking.
  There were bugs before refactoring and likely bugs added after refactoring.
  Please thoroughly review this code it you are using connection points.
  
  Some of this mirrors the MS COM tutorial code sample (connect.cpp).
*/

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <ocidl.h>
#include <algorithm>

#include "tLuaCOM.h"
#include "tLuaCOMConnPoints.h"
#include "tCOMUtil.h"
#include "tLuaCOMException.h"
#include "LuaAux.h"

#include "luacom_internal.h"
#include "LuaCompat.h"

static const char * CONNPOINT_NAME = "__LUACOM_CONNPOINT";


// first connection point cookie value
enum { COOKIE_START_VALUE = 400 };


///
/// tLuaCOMEnumConnPoints
///



/**
  Constructor.
  pHost - object to enumerate connection points for
*/
tLuaCOMEnumConnPoints::tLuaCOMEnumConnPoints(IUnknown* pHost) :
  mRefCount(0),
  mpHost(pHost),
  mPoints(),
  mNextIndex(0)
{
}


/**
  Destructor.
*/
tLuaCOMEnumConnPoints::~tLuaCOMEnumConnPoints()
{
}


/**
  Initialization.
*/
HRESULT tLuaCOMEnumConnPoints::Init(
    const std::vector<tCOMPtr<IConnectionPoint> > & points, ULONG nextIndex)
{
  if(nextIndex > points.size())
    return E_INVALIDARG;

  try
  {
    mPoints.assign(points.begin(), points.end());
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }
  mNextIndex = nextIndex;
  return S_OK;
}


/**
  IUnknown::QueryInterface implementation.
*/
STDMETHODIMP tLuaCOMEnumConnPoints::QueryInterface(REFIID riid, void ** ppv)
{
  if(!ppv)
    return E_POINTER;

  *ppv = (riid == IID_IUnknown || riid == IID_IEnumConnectionPoints) ?
         static_cast<void*>(this) : NULL;
  if (! *ppv)
    return E_NOINTERFACE;
  reinterpret_cast<IUnknown*>(*ppv)->AddRef();
  return S_OK;
}


/**
  IUnknown::AddRef implementation.
*/
STDMETHODIMP_(ULONG) tLuaCOMEnumConnPoints::AddRef()
{
  mpHost->AddRef();
  return ++mRefCount;
}


/**
  IUnknown::Release implementation.
*/
STDMETHODIMP_(ULONG) tLuaCOMEnumConnPoints::Release()
{
  ULONG refs = --mRefCount;
  mpHost->Release();
  if (refs == 0)
  {
    delete this;
    return 0;
  }
  return refs;
}


/**
  IEnumConnectionPoints::Next implementation.
*/
STDMETHODIMP tLuaCOMEnumConnPoints::Next(
    ULONG cReq, IConnectionPoint** paConnPts, ULONG* pcEnumerated)
{
  // check args
  if (! paConnPts)
    return E_POINTER;
  if (!pcEnumerated && cReq != 1)
    return E_POINTER;
  if (cReq == 0)
  {
    if (pcEnumerated)
      *pcEnumerated = 0;
    return S_OK;
  }
  if (!(mNextIndex < mPoints.size()))
  {
    if (pcEnumerated)
      *pcEnumerated = 0;
    return S_FALSE;
  }

  // copy
  size_t i = 0;
  for (; i < cReq && mNextIndex < mPoints.size(); i++, mNextIndex++)
  {
    paConnPts[i] = mPoints[mNextIndex];
    if (paConnPts[i]) paConnPts[i]->AddRef();
  }
  if (pcEnumerated)
    *pcEnumerated = i;

  return i == cReq ? S_OK : S_FALSE;
}


/**
  IEnumConnectionPoints::Skip implementation.
*/
STDMETHODIMP tLuaCOMEnumConnPoints::Skip(ULONG cSkip)
{
  size_t remaining = mPoints.size() - mNextIndex;
  if (cSkip > remaining)
  {
    mNextIndex = static_cast<ULONG>(mPoints.size());
    return S_FALSE;
  }
  mNextIndex += cSkip;
  return S_OK;
}


/**
  IEnumConnectionPoints::Reset implementation.
*/
STDMETHODIMP tLuaCOMEnumConnPoints::Reset()
{
  mNextIndex = 0;
  return S_OK;
}


/**
  IEnumConnectionPoints::Clone implementation.
*/
STDMETHODIMP tLuaCOMEnumConnPoints::Clone(IEnumConnectionPoints** ppIEnum)
{
  if(!ppIEnum)
    return E_POINTER;

  HRESULT hr;
  
  *ppIEnum = NULL;
  tLuaCOMEnumConnPoints* penumcp = NULL;
  try
  {
    penumcp = new tLuaCOMEnumConnPoints(mpHost);
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }
  hr = penumcp->Init(mPoints, mNextIndex);
  if (SUCCEEDED(hr))
    hr = penumcp->QueryInterface(
          IID_IEnumConnectionPoints, reinterpret_cast<void **>(ppIEnum));
  if (FAILED(hr))
    delete penumcp;

  return hr;
}



///
/// tLuaCOMConnPoint
///



/**
  Constructor.
*/
tLuaCOMConnPoint::tLuaCOMConnPoint(lua_State *p_L, IUnknown* pHost) :
  mpHost(pHost),
  mNextCookie(COOKIE_START_VALUE),
  mConnections(), mCookies(),
  L(p_L)
{
  // creates a new lua tag associated with this connection point
  luaCompat_pushTypeByName(L, MODULENAME, LCOM_CONNPOINT_TYPENAME);
  lua_pushcfunction(L, l_tagmeth_index);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
}


tLuaCOMConnPoint::~tLuaCOMConnPoint()
{
  for (size_t i=0; i<mSinks.size(); i++)
  {
    if (mSinks[i]) mSinks[i]->Unlock();
  }
}


HRESULT tLuaCOMConnPoint::Init(REFIID rIIDSink, ITypeInfo *pTypeinfo)
{
  for(size_t i = 0; i < mSinks.size(); ++i)
    if(mSinks[i])
      mSinks[i]->Unlock();

  mIIDSink = rIIDSink;
  mpTypeinfo = tCOMPtr<ITypeInfo>(pTypeinfo);
  mConnections.clear();
  mCookies.clear();
  mSinks.clear();
  return S_OK;
}



STDMETHODIMP tLuaCOMConnPoint::QueryInterface(REFIID riid, void ** ppv)
{
  if(!ppv)
    return E_POINTER;

  HRESULT hr = E_NOINTERFACE;

  *ppv = NULL;
  if (IID_IUnknown == riid || IID_IConnectionPoint == riid)
    *ppv = static_cast<void*>(this);
  if (*ppv)
  {
    reinterpret_cast<IUnknown*>(*ppv)->AddRef();
    hr = S_OK;
  }

  return hr;
}


STDMETHODIMP_(ULONG) tLuaCOMConnPoint::AddRef()
{
  return mpHost->AddRef();
}


STDMETHODIMP_(ULONG) tLuaCOMConnPoint::Release()
{
  return mpHost->Release();
}


/**
  IConnectionPoint::GetConnectionInterface implementation.
*/
STDMETHODIMP tLuaCOMConnPoint::GetConnectionInterface(IID* pIIDSink)
{
  if (!pIIDSink)
    return E_POINTER;
  *pIIDSink = mIIDSink;
  return S_OK;
}


/**
  IConnectionPoint::GetConnectionPointContainer implementation.
*/
STDMETHODIMP tLuaCOMConnPoint::GetConnectionPointContainer(
    IConnectionPointContainer** ppConnPtCon)
{
  if(!ppConnPtCon)
    return E_POINTER;

  HRESULT hr = mpHost->QueryInterface(
      IID_IConnectionPointContainer, reinterpret_cast<void **>(ppConnPtCon));
  return hr;
}


/**
  IConnectionPoint::Advise implementation.
*/
STDMETHODIMP tLuaCOMConnPoint::Advise(IUnknown* pUnkSink, DWORD* pdwCookie)
{
  HRESULT hr;

  if(!pUnkSink || !pdwCookie)
    return E_POINTER;

  *pdwCookie = 0;

  tCOMPtr<IDispatch> psink;
  hr = pUnkSink->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(&psink));
  if (FAILED(hr))
     return CONNECT_E_CANNOTCONNECT;

  // find a free location and reserve all parallel storage before creating the sink
  std::vector<DWORD>::iterator free = std::find(mCookies.begin(), mCookies.end(), 0);
  bool append = free == mCookies.end();
  size_t ifree = append ? mCookies.size() : free - mCookies.begin();

  if(append)
  {
    try
    {
      mConnections.reserve(mConnections.size() + 1);
      mCookies.reserve(mCookies.size() + 1);
      mSinks.reserve(mSinks.size() + 1);
    }
    catch (...)
    {
      return E_OUTOFMEMORY;
    }
  }

  tLuaCOM * sink = NULL;
  try
  {
    // VB supplies unusual type information, so use the source interface type information.
    sink = tLuaCOM::CreateLuaCOM(L, psink, IID_NULL, mpTypeinfo);
  }
  catch(class tLuaCOMException&)
  {
    return CONNECT_E_CANNOTCONNECT;
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }

  // Commit all parallel state only after sink creation succeeds.
  if(append)
  {
    mConnections.push_back(tCOMPtr<IUnknown>(psink.Raw()));
    mCookies.push_back(mNextCookie);
    mSinks.push_back(sink);
  }
  else
  {
    mConnections[ifree] = tCOMPtr<IUnknown>(psink.Raw());
    mCookies[ifree] = mNextCookie;
    mSinks[ifree] = sink;
  }

  *pdwCookie = mNextCookie;
  mNextCookie++;

  return S_OK;
}


/**
  IConnectionPoint::Unadvise implementation.
*/
STDMETHODIMP tLuaCOMConnPoint::Unadvise(DWORD dwCookie)
{
  if (dwCookie == 0)
    return E_UNEXPECTED;

  // find location
  std::vector<DWORD>::iterator it
      = std::find(mCookies.begin(), mCookies.end(), dwCookie);
  if (it == mCookies.end())
    return CONNECT_E_NOCONNECTION;
  size_t pos = it - mCookies.begin();

  // remove it
  mConnections[pos].Release();  
  mCookies[pos] = 0;
  if(mSinks[pos])
    mSinks[pos]->Unlock();
  mSinks[pos] = NULL;

  return S_OK;
}


/**
  IConnectionPoint::EnumConnections implementation.
*/
STDMETHODIMP tLuaCOMConnPoint::EnumConnections(IEnumConnections** ppIEnum)
{
  if(!ppIEnum)
    return E_POINTER;

  HRESULT hr;
  *ppIEnum = NULL;

  // copy only non-empty elements
  std::vector<tCOMPtr<IUnknown> > connections;
  std::vector<DWORD> cookies;
  try
  {
    connections.reserve(mConnections.size());
    cookies.reserve(mCookies.size());
    for (size_t i=0; i < mConnections.size(); i++)
    {
      if (mCookies[i] != 0)
      {
        connections.push_back(mConnections[i]);
        cookies.push_back(mCookies[i]);
      }
    }
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }

  // create IEnumConnections.
  tLuaCOMEnumConnections* penumc = NULL;
  try
  {
    penumc = new tLuaCOMEnumConnections(this);
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }
  hr = penumc->Init(connections, cookies, 0);
  if (SUCCEEDED(hr))
    hr = penumc->QueryInterface(
          IID_IEnumConnections, reinterpret_cast<void **>(ppIEnum));
  if (FAILED(hr))
    delete penumc;

  return hr;
}



///
/// tLuaCOMEnumConnections
///



/**
  Constructor.
  pHost - object to be enumerated
*/
tLuaCOMEnumConnections::tLuaCOMEnumConnections(IUnknown* pHost) :
  mRefCount(0), mpHost(pHost),
  mConnections(), mCookies(),
  mNextIndex(0)
{
}


/**
  Destructor.
*/
tLuaCOMEnumConnections::~tLuaCOMEnumConnections()
{
}


/**
  Initialization.
*/
HRESULT tLuaCOMEnumConnections::Init(
    std::vector<tCOMPtr<IUnknown> > & connections,
    std::vector<DWORD> & cookies, ULONG nextIndex)
{
  if(connections.size() != cookies.size())
    return E_INVALIDARG;
  if(nextIndex > connections.size())
    return E_INVALIDARG;

  try
  {
    mConnections = connections;
    mCookies = cookies;
  }
  catch (...)
  {
    mConnections.clear();
    mCookies.clear();
    return E_OUTOFMEMORY;
  }
  mNextIndex = nextIndex;
  return S_OK;
}


/**
  IUnknown::QueryInterface implementation.
*/
STDMETHODIMP tLuaCOMEnumConnections::QueryInterface(
    REFIID riid, void ** ppv)
{
  if(!ppv)
    return E_POINTER;

  if (riid == IID_IUnknown || riid == IID_IEnumConnections)
    *ppv = static_cast<void*>(this);
  else
    *ppv = NULL;

  if (*ppv)
  {
    reinterpret_cast<IUnknown*>(*ppv)->AddRef();
    return S_OK;
  }
  else
    return E_NOINTERFACE;
}


/**
  IUnknown::AddRef implementation.
*/
STDMETHODIMP_(ULONG) tLuaCOMEnumConnections::AddRef()
{
  mpHost->AddRef();
  return ++mRefCount;
}


/**
  IUnknown::Release implementation.
*/
STDMETHODIMP_(ULONG) tLuaCOMEnumConnections::Release()
{
  ULONG refs = --mRefCount;
  mpHost->Release();
  if (refs == 0)
  {
    delete this;
    return 0;
  }
  return refs;
}


/**
  IEnumConnections::Next implementation.
*/
STDMETHODIMP tLuaCOMEnumConnections::Next(
    ULONG cReq, CONNECTDATA* paConnections, ULONG* pcEnumerated)
{
  // check bad arguments
  if (!paConnections)
    return E_POINTER;
  if (!pcEnumerated && cReq != 1)
    return E_POINTER;
  if (cReq == 0)
  {
    if (pcEnumerated)
      *pcEnumerated = 0;
    return S_OK;
  }
  if (!(mNextIndex < mConnections.size()))
    {
    if (pcEnumerated)
      *pcEnumerated = 0;
    return S_FALSE;
    }

  // copy values and advance mNextIndex.
  unsigned int i = 0;
  for ( ; i<cReq && mNextIndex < mConnections.size(); i++, mNextIndex++)
    {
    if (mConnections[mNextIndex]) mConnections[mNextIndex]->AddRef();
    paConnections[i].pUnk     = mConnections[mNextIndex];
    paConnections[i].dwCookie = mCookies[mNextIndex];
    }

  // number of values returned
  if (pcEnumerated)
    *pcEnumerated = i;

  return i == cReq ? S_OK : S_FALSE;
}


/**
  IEnumConnections::Skip implementation.
*/
STDMETHODIMP tLuaCOMEnumConnections::Skip(ULONG cSkip)
{
  size_t remaining = mConnections.size() - mNextIndex;
  if (cSkip > remaining)
  {
    mNextIndex = static_cast<ULONG>(mConnections.size());
    return S_FALSE;
  }
  mNextIndex += cSkip;
  return S_OK;
}


/**
  IEnumConnections::Reset implementation.
*/
STDMETHODIMP tLuaCOMEnumConnections::Reset()
{
  mNextIndex = 0;
  return S_OK;
}


/**
  IEnumConnections::Clone implementation.
*/
STDMETHODIMP tLuaCOMEnumConnections::Clone(IEnumConnections** ppIEnum)
{
  if(!ppIEnum)
    return E_POINTER;

  HRESULT hr;
  *ppIEnum = NULL;

  tLuaCOMEnumConnections* penumc = NULL;
  try
  {
    penumc = new tLuaCOMEnumConnections(mpHost);
  }
  catch (...)
  {
    return E_OUTOFMEMORY;
  }
  hr = penumc->Init(mConnections, mCookies, mNextIndex);
  if (SUCCEEDED(hr))
    hr = penumc->QueryInterface(
        IID_IEnumConnections, reinterpret_cast<void **>(ppIEnum));
  if (FAILED(hr))
    delete penumc;

  return hr;
}



///
/// tLuaCOMConnPointContainer
///



/**
  Constructor.
*/
tLuaCOMConnPointContainer::tLuaCOMConnPointContainer(lua_State* pL, IUnknown* pOuter) :
  mpOuter(pOuter),
  mDefaultPoint(NULL),
  L(pL)
{
  CHECKPARAM(pL); CHECKPARAM(pOuter);

  // creates connection point for source interface
  tCOMPtr<IProvideClassInfo2> ci2;
  CHK_COM_CODE(mpOuter->QueryInterface(IID_IProvideClassInfo2, reinterpret_cast<void **>(&ci2))); 
  IID iid;
  CHK_COM_CODE(ci2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &iid));
  tCOMPtr<ITypeInfo> coclassinfo;
  CHK_COM_CODE(ci2->GetClassInfo(&coclassinfo));
  tCOMPtr<ITypeInfo> events_typeinfo;
  events_typeinfo.Attach(tCOMUtil::GetDefaultInterfaceTypeInfo(coclassinfo, true));
  CHK_LCOM_ERR(events_typeinfo, "No default source typeinfo.");
  tLuaCOMConnPoint* point = new tLuaCOMConnPoint(L, mpOuter);
  try
  {
    CHK_COM_CODE(point->Init(iid, events_typeinfo));
    mPoints.push_back(point);
  }
  catch(...)
  {
    delete point;
    throw;
  }
  mDefaultPoint = point;
}



tLuaCOMConnPointContainer::~tLuaCOMConnPointContainer()
{
  for(size_t i = 0; i < mPoints.size(); i++)
    delete mPoints[i];
}


STDMETHODIMP tLuaCOMConnPointContainer::QueryInterface(REFIID riid, void ** ppv)
{
  return mpOuter->QueryInterface(riid, ppv);
}



STDMETHODIMP_(ULONG) tLuaCOMConnPointContainer::AddRef()
{
  return mpOuter->AddRef();
}


STDMETHODIMP_(ULONG) tLuaCOMConnPointContainer::Release()
{
  return mpOuter->Release();
}


/**
  IConnectionPointContainer::FindConnectionPoint implementation.
*/
STDMETHODIMP tLuaCOMConnPointContainer::FindConnectionPoint(
     REFIID riid, IConnectionPoint** ppConnPt)
{
  if(!ppConnPt)
    return E_POINTER;

  *ppConnPt = NULL;

  HRESULT hr = E_NOINTERFACE;
  if (mDefaultPoint)
  {
    IID iid;
    hr = mDefaultPoint->GetConnectionInterface(&iid);
    if(FAILED(hr))
      return hr;

    if(iid == riid)
    {
      hr = mDefaultPoint->QueryInterface(IID_IConnectionPoint,
                 reinterpret_cast<void **>(ppConnPt));
    }
    else
    {
      hr = CONNECT_E_NOCONNECTION;
    }
  }

  return hr;
}


/**
  IConnectionPointContainer::EnumConnectionPoints implementation.
*/
STDMETHODIMP tLuaCOMConnPointContainer::EnumConnectionPoints(
    IEnumConnectionPoints** ppIEnum)
{
  if(!ppIEnum)
    return E_POINTER;

  HRESULT hr = S_OK;
  
  // create IEnumConnectionPoints
  *ppIEnum = NULL;
  tLuaCOMEnumConnPoints* penumcp = NULL;
  std::vector<tCOMPtr<IConnectionPoint> > points;
  try
  {
    penumcp = new tLuaCOMEnumConnPoints(this);
    points.reserve(mPoints.size());
    for(size_t i = 0; i < mPoints.size(); ++i)
      points.push_back(tCOMPtr<IConnectionPoint>(mPoints[i]));
  }
  catch (...)
  {
    delete penumcp;
    return E_OUTOFMEMORY;
  }

  hr = penumcp->Init(points, 0);
  if (FAILED(hr))
  {
    delete penumcp;
    return hr;
  }
  hr = penumcp->QueryInterface(
        IID_IEnumConnectionPoints, reinterpret_cast<void **>(ppIEnum));
  if (FAILED(hr))
    delete penumcp;

  return hr;
}


tLuaCOMConnPoint* tLuaCOMConnPointContainer::GetDefault()
{
  return mDefaultPoint;
}



///
/// tLuaCOMConnPoint
///



void tLuaCOMConnPoint::push()
{
  // Lua `setmetatable({[CONNPOINT_NAME]=this}, mt)`
  LUASTACK_SET(L);
  lua_newtable(L);
  luaCompat_pushTypeByName(L, MODULENAME, LCOM_CONNPOINT_TYPENAME);
  lua_setmetatable(L, -2);
  lua_pushstring(L, CONNPOINT_NAME);
  luaCompat_pushTypeByName(L, MODULENAME, LCOM_IUNKNOWN_TYPENAME);
  AddRef();
  luaCompat_newTypedObject(L, this);
  lua_settable(L, -3);
  LUASTACK_CLEAN(L, 1);
}

// v = __index(t, k)
int tLuaCOMConnPoint::l_tagmeth_index(lua_State *L)
{
  // Lua `self[CONNPOINT_NAME], k`
  lua_pushstring(L, CONNPOINT_NAME);
  lua_gettable(L, 1);  // self[CONNPOINT_NAME]
  lua_pushvalue(L, 2); // k (event name)

  // pushes closure to call all sinks
  lua_pushcclosure(L, l_call_sinks, 2);

  return 1;
}


/**
  lua C function that dispatches events to sinks
*/
int tLuaCOMConnPoint::l_call_sinks(lua_State *L)
{
  // positions of parameters
  const int self_param        = 1;  // self param
  const int user_first_param  = 2;  // first user param 
  const int user_last_param   = lua_gettop(L);  // last user param
  const int connpoint         = lua_upvalueindex(1);
  const int event             = lua_upvalueindex(2);

  int num_params = (std::max)(0, user_last_param - user_first_param + 1);

  // gets connection point
  tLuaCOMConnPoint* cp =
    *reinterpret_cast<tLuaCOMConnPoint**>(lua_touserdata(L, connpoint));

  // call each sink
  for(size_t i = 0; i < cp->mConnections.size(); i++)
  {
    if(cp->mCookies[i] == 0 || cp->mSinks[i] == NULL)
      continue;

    // pushes function mSinks[i][event] and lock
    LuaBeans::push(L, cp->mSinks[i]);
    cp->mSinks[i]->Lock();
    lua_pushvalue(L, event);
    lua_gettable(L, -2);
    lua_remove(L, -2);

    // self param (mandatory but unused)
    LuaBeans::push(L, cp->mSinks[i]);
    cp->mSinks[i]->Lock();

    // duplicates parameters (if any)
    for(int j = user_first_param; j <= user_last_param; j++)
    {
      lua_pushvalue(L, j);
    }

    // calls function (including self param), ignoring errors
    luaCompat_call(L, num_params+1, 0);

    // cleans stack
    lua_settop(L, user_last_param);
  }

  return 0;
}
