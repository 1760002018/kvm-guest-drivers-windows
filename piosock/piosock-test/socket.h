#pragma once

//////////////////////////////////////////////////////////////////////////
SOCKET
WINAPI
PIOSockCreateSocket(
    _In_opt_ PPHYZIO_VSOCK_PARAMS pSocketParams
);

VOID
WINAPI
PIOSockCloseSocket(
    _In_ SOCKET hSocket
);

BOOL
WINAPI
PIOSockGetConfig(
    _In_ SOCKET hPiosock,
    _Out_ PPHYZIO_VSOCK_CONFIG pConfig
);
