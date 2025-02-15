#pragma once

#include "tstrings.h"

using std::exception;

class neTPTVException : public exception
{
public:
    neTPTVException();
    neTPTVException(LPCTSTR lpzMessage);
    neTPTVException(const tstring &Message);
    neTPTVException(const neTPTVException &Other);
    virtual ~neTPTVException();

    virtual const char *what() const;
    virtual LPCTSTR     twhat() const;

protected:
    void SetMessage(const tstring &Message);

private:
    tstring m_Message;
    string  m_MBCSMessage;
};

class neTPTVNumErrorException : public neTPTVException
{
public:
    neTPTVNumErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode);
    neTPTVNumErrorException(const tstring &Description, DWORD dwErrorCode);
    neTPTVNumErrorException(const neTPTVNumErrorException& Other);

    DWORD GetErrorCode(void) { return m_dwErrorCode; }

protected:
    DWORD m_dwErrorCode;
};

class neTPTVCRTErrorException : public neTPTVNumErrorException
{
public:
    neTPTVCRTErrorException(int nErrorCode = errno);
    neTPTVCRTErrorException(LPCTSTR lpzDescription, int nErrorCode = errno);
    neTPTVCRTErrorException(const tstring &Description, int nErrorCode = errno);
    neTPTVCRTErrorException(const neTPTVCRTErrorException &Other);

protected:
    static tstring GetErrorString(DWORD dwErrorCode);
};

#ifdef WIN32
class neTPTVW32ErrorException : public neTPTVNumErrorException
{
public:
    neTPTVW32ErrorException(DWORD dwErrorCode = GetLastError());
    neTPTVW32ErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode = GetLastError());
    neTPTVW32ErrorException(const tstring &Description, DWORD dwErrorCode = GetLastError());
    neTPTVW32ErrorException(const neTPTVW32ErrorException &Other);

protected:
    static tstring GetErrorString(DWORD dwErrorCode);
};
#endif

