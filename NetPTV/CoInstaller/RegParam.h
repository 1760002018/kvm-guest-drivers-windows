#pragma once

#include "tstrings.h"
#include "RegAccess.h"

enum neTPTVRegParamType
{
    NETPTV_RTT_ENUM,
    NETPTV_RTT_INT,
    NETPTV_RTT_LONG,
    NETPTV_RTT_EDIT,
    NETPTV_RTT_LAST
};

#define NETPTV_RTT_UNKNOWN NETPTV_RTT_LAST

////////////////////////////////////////////////////////////////////////////////
// NOTE: Basic Parameter Info can be accessed via the public neTPTVRegParam API:
//  neTPTVRegParam::GetName()
//  neTPTVRegParam::GetDescription()
//  neTPTVRegParam::IsOptional()
//  neTPTVRegParam::GetType()
//  neTPTVRegParam::GetValue()
// Extended Parameter Info can be accessed via the neTPTVRegParam::FillExInfo API.
////////////////////////////////////////////////////////////////////////////////

enum neTPTVRegParamExInfoIDType
{
    NETPTV_RPIID_ENUM_VALUE,      // Always followed by NETPTV_RPIID_ENUM_VALUE_DESC
    NETPTV_RPIID_ENUM_VALUE_DESC, // Always follows the NETPTV_RPIID_ENUM_VALUE
    NETPTV_RPIID_NUM_MIN,
    NETPTV_RPIID_NUM_MAX,
    NETPTV_RPIID_NUM_STEP,
    NETPTV_RPIID_EDIT_TEXT_LIMIT,
    NETPTV_RPIID_EDIT_UPPER_CASE,
    NETPTV_RPIID_LAST
};

typedef pair <neTPTVRegParamExInfoIDType, tstring> neTPTVRegParamExInfo;

typedef list<neTPTVRegParamExInfo> neTPTVRegParamExInfoList;

class neTPTVRegParamBadNameException : public neTPTVException
{
public:
    neTPTVRegParamBadNameException() :
      neTPTVException(TEXT("Invalid Registry Parameter Name"))
    { }
};

class neTPTVRegParamBadTypeException : public neTPTVException
{
public:
    neTPTVRegParamBadTypeException() :
      neTPTVException(TEXT("Invalid Registry Parameter Type"))
    { }
};

class neTPTVRegParamBadRegistryException : public neTPTVException
{
public:
    neTPTVRegParamBadRegistryException() :
      neTPTVException(TEXT("Invalid Registry Parameter Data"))
    { }
};

class neTPTVRegParam
{
public:
    static neTPTVRegParamType GetType(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
    static neTPTVRegParam    *GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
    static neTPTVRegParam    *GetParam(neTPTVRegAccess &DevParamsRegKey, DWORD dwIndex);

    virtual ~neTPTVRegParam(void);

    const tstring &GetName(void) const { return m_Name; }
    const tstring &GetValue(void) const { return m_Value; }
    const tstring &GetDescription(void) const { return m_Description; }
    bool           IsOptional(void) const { return m_bOptional; }

    bool ValidateAndSetValue(LPCTSTR pszValue)
    {
        if (ValidateValue(pszValue))
        {
            SetValue(pszValue);
            return true;
        }

        return false;
    }

    bool Save(void);

    virtual void FillExInfo(neTPTVRegParamExInfoList &ExInfoList);

protected:
    neTPTVRegParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    void SetValue(LPCTSTR pszValue)
    {
        m_Value = pszValue;
    }

    void SetDescription(LPCTSTR pszDescription)
    {
        m_Description = pszDescription;
    }

    virtual neTPTVRegParamType GetType(void) const = 0;
    virtual bool            ValidateValue(LPCTSTR pszValue) = 0;
    virtual void            Load(void);

    tstring       m_Name;
    tstring       m_Description;
    tstring       m_Value;
    bool          m_bOptional;
    tstring       m_ParamRegSubKey;
    neTPTVRegAccess &m_DevParamsRegKey;
};

class neTPTVRegEnumParam : public neTPTVRegParam
{
    friend static neTPTVRegParam *neTPTVRegParam::GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual void FillExInfo(neTPTVRegParamExInfoList &ExInfoList);

protected:
    neTPTVRegEnumParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTPTVRegParamType GetType(void) const
    {
        return NETPTV_RTT_ENUM;
    }

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    neTPTVTStrList m_Values;
    neTPTVTStrList m_ValueDescs;
};

template <class INT_T>
class neTPTVRegNumParam : public neTPTVRegParam
{
    friend static neTPTVRegParam *neTPTVRegParam::GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual ~neTPTVRegNumParam();

    virtual void FillExInfo(neTPTVRegParamExInfoList &ExInfoList);

protected:
    neTPTVRegNumParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    INT_T m_nMin;
    INT_T m_nMax;
    INT_T m_nStep;
};

class neTPTVRegIntParam : public neTPTVRegNumParam<unsigned int>
{
    friend static neTPTVRegParam *neTPTVRegParam::GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
protected:
    neTPTVRegIntParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTPTVRegParamType GetType(void) const
    {
        return NETPTV_RTT_INT;
    }
};

class neTPTVRegLongParam : public neTPTVRegNumParam<unsigned long>
{
    friend static neTPTVRegParam *neTPTVRegParam::GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
protected:
    neTPTVRegLongParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTPTVRegParamType GetType(void) const
    {
        return NETPTV_RTT_LONG;
    }
};

class neTPTVRegEditParam : public neTPTVRegParam
{
    friend static neTPTVRegParam *neTPTVRegParam::GetParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual void FillExInfo(neTPTVRegParamExInfoList &ExInfoList);

protected:
    neTPTVRegEditParam(neTPTVRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTPTVRegParamType GetType(void) const
    {
        return NETPTV_RTT_EDIT;
    }

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    DWORD m_nLimitText;
    bool  m_bUpperCase;
};

