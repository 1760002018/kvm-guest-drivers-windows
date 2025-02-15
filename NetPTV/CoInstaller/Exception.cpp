#include "stdafx.h"
#include "Exception.h"

neTPTVException::neTPTVException()
{

}

neTPTVException::neTPTVException(LPCTSTR lpzMessage)
{
    if (lpzMessage)
    {
        SetMessage(tstring(lpzMessage));
    }
}

neTPTVException::neTPTVException(const tstring &Message)
{
    SetMessage(Message);
}

neTPTVException::neTPTVException(const neTPTVException &Other)
{
    SetMessage(Other.m_Message);
}

neTPTVException::~neTPTVException(void)
{

}

const char *neTPTVException::what() const
{
    return m_MBCSMessage.c_str();
}

LPCTSTR neTPTVException::twhat() const
{
    return m_Message.c_str();
}

void neTPTVException::SetMessage(const tstring &Message)
{
    m_Message     = Message;
    m_MBCSMessage = tstring2string(m_Message);
}

neTPTVNumErrorException::neTPTVNumErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode)
    : neTPTVException(lpzDescription), m_dwErrorCode(dwErrorCode)
{

}

neTPTVNumErrorException::neTPTVNumErrorException(const tstring &Description, DWORD dwErrorCode)
    : neTPTVException(Description), m_dwErrorCode(dwErrorCode)
{

}

neTPTVNumErrorException::neTPTVNumErrorException(const neTPTVNumErrorException& Other)
    : neTPTVException(Other)
{
    m_dwErrorCode = Other.m_dwErrorCode;
}

neTPTVCRTErrorException::neTPTVCRTErrorException(int nErrorCode)
    : neTPTVNumErrorException(GetErrorString(nErrorCode), (DWORD)nErrorCode)
{

}

neTPTVCRTErrorException::neTPTVCRTErrorException(LPCTSTR lpzDescription, int nErrorCode)
    : neTPTVNumErrorException(tstring(lpzDescription) + GetErrorString((DWORD)nErrorCode), (DWORD)nErrorCode)
{

}

neTPTVCRTErrorException::neTPTVCRTErrorException(const tstring &Description, int nErrorCode)
: neTPTVNumErrorException(Description + GetErrorString((DWORD)nErrorCode), (DWORD)nErrorCode)
{

}

neTPTVCRTErrorException::neTPTVCRTErrorException(const neTPTVCRTErrorException &Other)
    : neTPTVNumErrorException(Other)
{

}

tstring neTPTVCRTErrorException::GetErrorString(DWORD dwErrorCode)
{
#ifdef WIN32
    TCHAR tcaBuff[256];
    _tcserror_s(tcaBuff, TBUF_SIZEOF(tcaBuff), (int)dwErrorCode);
    return tcaBuff;
#else
    return string2tstring(strerror((int)dwErrorCode));
#endif
}

#ifdef WIN32
neTPTVW32ErrorException::neTPTVW32ErrorException(DWORD dwErrorCode)
    : neTPTVNumErrorException(GetErrorString(m_dwErrorCode), dwErrorCode)
{

}

neTPTVW32ErrorException::neTPTVW32ErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode)
    : neTPTVNumErrorException(tstring(lpzDescription) + GetErrorString(dwErrorCode), dwErrorCode)
{

}

neTPTVW32ErrorException::neTPTVW32ErrorException(const tstring &Description, DWORD dwErrorCode)
    : neTPTVNumErrorException(Description + GetErrorString(dwErrorCode), dwErrorCode)
{

}

neTPTVW32ErrorException::neTPTVW32ErrorException(const neTPTVW32ErrorException &Other)
    : neTPTVNumErrorException(Other)
{

}

tstring neTPTVW32ErrorException::GetErrorString(DWORD dwErrorCode)
{
    LPVOID lpMsgBuf;
    DWORD  msgLen = ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS|
                                    FORMAT_MESSAGE_MAX_WIDTH_MASK,
                                    NULL,
                                    dwErrorCode,
                                    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                    (LPTSTR) &lpMsgBuf,
                                    0,
                                    NULL);
    if(msgLen == 0)
    {
        tstringstream strm;
        strm << TEXT("Failed to get error description for error code: 0x") << hex << dwErrorCode;
        return strm.str();
    }
    else
    {
        tstring strResult((LPCTSTR)lpMsgBuf, msgLen);
        ::LocalFree( lpMsgBuf );
        return strResult;
    }
}
#endif // WIN32
