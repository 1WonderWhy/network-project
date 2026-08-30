#pragma once

#ifdef _WIN32
    // ---------- Windows ----------
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    typedef int socklen_t;

    #define CLOSESOCKET closesocket

    inline bool netInit() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }
    inline void netCleanup() { WSACleanup(); }

#else
    // ---------- Linux / Mac (POSIX) ----------
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>

    #define CLOSESOCKET close

    inline bool netInit() { return true; }   // ไม่ต้องทำอะไรบน Linux/Mac
    inline void netCleanup() {}               // ไม่ต้องทำอะไรบน Linux/Mac

#endif
