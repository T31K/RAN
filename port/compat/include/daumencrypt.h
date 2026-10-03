// The non-Windows branch of DaumGameCrypt.h includes <daumencrypt.h>, which Daum (the Korean
// portal behind the server-side login crypto) never shipped. This supplies the same
// declarations as that header's _WIN32 branch. The client only compiles them (they arrive via
// GLGaeaServer.h -> s_CServer.h); it never instantiates the Daum COM objects.
#pragma once
#include "ran_compat.h"

#ifndef MIDL_INTERFACE
#define MIDL_INTERFACE(uuid) struct
#endif

const IID IID_IEncrypt =    {0x40E692F5,0xCC5D,0x4609,{0x94,0x8D,0x09,0x88,0x12,0x5F,0xF8,0xB4}};
const IID IID_IDecrypt =    {0xBA5723E0,0x7F1C,0x4418,{0x94,0x91,0x21,0x19,0xEF,0x75,0xEB,0x83}};
const CLSID CLSID_Encrypt = {0x285659A3,0x6F2B,0x4036,{0x94,0xAC,0x53,0x4E,0x4D,0x23,0x88,0x86}};
const CLSID CLSID_Decrypt = {0x9AC5B738,0x6E08,0x4148,{0xB3,0x37,0x63,0xCE,0xC5,0x09,0x0C,0x49}};

MIDL_INTERFACE("40E692F5-CC5D-4609-948D-0988125FF8B4")
IEncrypt : public IDispatch
{
public:
    virtual HRESULT STDMETHODCALLTYPE Init(BSTR key) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Input(BSTR* pVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Input(BSTR newVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Output(BSTR* pVal) = 0;
};

MIDL_INTERFACE("BA5723E0-7F1C-4418-9491-2119EF75EB83")
IDecrypt : public IDispatch
{
public:
    virtual HRESULT STDMETHODCALLTYPE Init(BSTR key) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Input(BSTR* pVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Input(BSTR newVal) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Output(BSTR* pVal) = 0;
};
