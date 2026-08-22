#ifndef INCLUDES_WS_H
#define INCLUDES_WS_H

// The Xbox reaches the network through nxdk's lwIP, which is not the BSD
// socket layer this was written against and has to be brought up by the title
// before it will do anything. Rather than half-wire it, the socket calls become
// failures here: a game can still ask for a connection, and simply never gets
// one. The engine's networking is documented as not working anyway.
#ifdef XBOX
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stdint.h>

    // Only what the class declaration needs to name. Nothing on the Xbox calls
    // through any of it.
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define socketerrno 0
    #define SOCKET_EAGAIN_EINPROGRESS 0
    #define SOCKET_EWOULDBLOCK 0
#elif defined(_WIN32)
    #if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
        #define _CRT_SECURE_NO_WARNINGS // _CRT_SECURE_NO_WARNINGS for sscanf errors in MSVC2013 Express
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <fcntl.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/types.h>
    #include <io.h>
    #ifndef _SSIZE_T_DEFINED
        typedef int ssize_t;
        #define _SSIZE_T_DEFINED
    #endif
    #ifndef _SOCKET_T_DEFINED
        typedef SOCKET socket_t;
        #define _SOCKET_T_DEFINED
    #endif
    #ifndef snprintf
        #define snprintf _snprintf_s
    #endif
    #include <stdint.h>
    #define socketerrno WSAGetLastError()
    #define SOCKET_EAGAIN_EINPROGRESS WSAEINPROGRESS
    #define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#else
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <unistd.h>
    #ifndef _SOCKET_T_DEFINED
        typedef int socket_t;
        #define _SOCKET_T_DEFINED
    #endif
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif
    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR   (-1)
    #endif
    #define closesocket(s) close(s)
    #include <errno.h>
    #define socketerrno errno
    #define SOCKET_EAGAIN_EINPROGRESS EAGAIN
    #define SOCKET_EWOULDBLOCK EWOULDBLOCK
#endif

#include <vector>

#endif /* INCLUDES_WS_H */
