// piosock-test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "socket.h"

ULONG64
GetMoozeCid()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hPiosock = PIOSockCreateSocket(NULL);
    PHYZIO_VSOCK_CONFIG vsConfig;

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    if (hPiosock == INVALID_SOCKET)
    {
        _tprintf(L"PIOSockCreateSocket error: %d\n", GetLastError());
        return uRes;
    }

    if (PIOSockGetConfig(hPiosock, &vsConfig))
        uRes = vsConfig.mooze_cid;
    else
        _tprintf(L"PIOSockGetConfig error: %d\n", GetLastError());

    PIOSockCloseSocket(hPiosock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

ULONG64
GetMoozeCidFromNewSocket()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hPiosock;
    PHYZIO_VSOCK_CONFIG vsConfig;
    PHYZIO_VSOCK_PARAMS SocketParams = { 0 };

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    hPiosock = PIOSockCreateSocket(&SocketParams);
    if (hPiosock == INVALID_SOCKET)
    {
        _tprintf(L"PIOSockCreateSocket(new) error: %d\n", GetLastError());
        return uRes;
    }

    if (PIOSockGetConfig(hPiosock, &vsConfig))
    {
        uRes = vsConfig.mooze_cid;
    }
    else
        _tprintf(L"PIOSockGetConfig error: %d\n", GetLastError());

    PIOSockCloseSocket(hPiosock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

ULONG64
GetMoozeCidFromAcceptSocket()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hListenSock, hAcceptSocket;
    PHYZIO_VSOCK_CONFIG vsConfig;
    PHYZIO_VSOCK_PARAMS SocketParams = { 0 };

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    hListenSock = PIOSockCreateSocket(&SocketParams);
    if (hListenSock == INVALID_SOCKET)
    {
        _tprintf(L"PIOSockCreateSocket(new) error: %d\n", GetLastError());
        return uRes;
    }

    SocketParams.Socket = hListenSock;
    hAcceptSocket = PIOSockCreateSocket(&SocketParams);
    if (hAcceptSocket == INVALID_SOCKET)
    {
        _tprintf(L"PIOSockCreateSocket(accept) error: %d\n", GetLastError());
        PIOSockCloseSocket(hListenSock);
        return uRes;
    }

    if (PIOSockGetConfig(hAcceptSocket, &vsConfig))
    {
        uRes = vsConfig.mooze_cid;
    }
    else
        _tprintf(L"PIOSockGetConfig error: %d\n", GetLastError());

    PIOSockCloseSocket(hAcceptSocket);
    PIOSockCloseSocket(hListenSock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

#define TEST_PORT 2222

int __cdecl main()
{
    ULONG64 uMoozeCid = GetMoozeCid();

    if (uMoozeCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetMoozeCid cid: %d\n", (DWORD)uMoozeCid);
    }

    uMoozeCid = GetMoozeCidFromNewSocket();
    if (uMoozeCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetMoozeCidFromNewSocket cid: %d\n", (DWORD)uMoozeCid);
    }

    uMoozeCid = GetMoozeCidFromAcceptSocket();
    if (uMoozeCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetMoozeCidFromAcceptSocket cid: %d\n", (DWORD)uMoozeCid);
    }

    return 0;
}
