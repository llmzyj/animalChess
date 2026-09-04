// AnimalChessNet.cpp : 联机模块实现（详见 AnimalChessNet.h）
#include "pch.h"
#include "AnimalChessNet.h"

#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {
const int NET_POLL_MS = 500;          // select 轮询间隔
const int NET_HANDSHAKE_TIMEOUT_MS = 10000;  // 握手接收超时
constexpr int NET_BODY_MAX = 64;      // 本协议所有消息包体都很小

struct NetHostJob {
    HWND         hwnd;
    SOCKET       listen;
    volatile LONG* pCancel;
};
struct NetClientJob {
    HWND         hwnd;
    SOCKET       sock;
    CString      ip;
    u_short      port;
    volatile LONG* pCancel;
};
struct NetGameJob {
    HWND         hwnd;
    SOCKET       sock;
};

void PackHeader(NetPacketHeader& h, uint16_t id, uint32_t len) {
    h.magic[0] = 0x55;
    h.magic[1] = 0xAA;
    h.msgId = id;
    h.length = len;
}

bool HeaderLooksValid(const NetPacketHeader& h) {
    return h.magic[0] == 0x55 && h.magic[1] == 0xAA && h.length <= NET_BODY_MAX;
}

bool NetSendBytes(SOCKET s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        sent += n;
    }
    return true;
}

bool NetRecvBytes(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

// 在指定时间(秒)内等待套接字可读；可被 pCancel 打断。返回 1 就绪/0 超时/-1 取消或错误
int NetWaitReadable(SOCKET s, volatile LONG* pCancel, int waitMs = NET_POLL_MS) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv;
    tv.tv_sec = waitMs / 1000;
    tv.tv_usec = (waitMs % 1000) * 1000;
    for (;;) {
        if (pCancel && InterlockedCompareExchange((volatile LONG*)pCancel, 0, 0) != 0)
            return -1;
        int r = select(0, &rd, nullptr, nullptr, &tv);
        if (r > 0) return 1;
        if (r == 0) return 0;        // 超时
        return -1;                    // 出错
    }
}

bool NetWaitWritable(SOCKET s, volatile LONG* pCancel) {
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(s, &wr);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = NET_POLL_MS * 1000;
    for (;;) {
        if (pCancel && InterlockedCompareExchange((volatile LONG*)pCancel, 0, 0) != 0)
            return false;
        int r = select(0, nullptr, &wr, nullptr, &tv);
        if (r > 0) return true;
        if (r == 0) continue;
        return false;
    }
}

bool NetSetRecvTimeout(SOCKET s, int ms) {
    DWORD t = (DWORD)ms;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t)) == 0;
}

void PostResult(HWND hwnd, NetConnectResult* r) {
    if (r->ok) {
        if (hwnd && ::IsWindow(hwnd)) ::PostMessage(hwnd, WM_LOBBY_NET_CONNECTED, 0, (LPARAM)r);
        else { if (r->sock != INVALID_SOCKET) closesocket(r->sock); delete r; }
    }
    else {
        if (hwnd && ::IsWindow(hwnd)) ::PostMessage(hwnd, WM_LOBBY_NET_FAILED, 0, (LPARAM)r);
        else { if (r->sock != INVALID_SOCKET) closesocket(r->sock); delete r; }
    }
}
} // namespace

// ---------------- 基础 ----------------
static bool g_netStarted = false;

bool Net_Startup() {
    if (g_netStarted) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_netStarted = true;
    return true;
}

void Net_Cleanup() {
    if (g_netStarted) {
        WSACleanup();
        g_netStarted = false;
    }
}

CString Net_DescribeError(int err) {
    switch (err) {
    case WSAETIMEDOUT:      return CString(L"连接超时");
    case WSAECONNREFUSED:   return CString(L"对方拒绝连接（主机未开启 / 端口不正确）");
    case WSAEHOSTUNREACH:   return CString(L"对方主机不可达（请检查 IP 与网络）");
    case WSAENETUNREACH:    return CString(L"网络不可达");
    case WSAECONNRESET:     return CString(L"连接被对方重置");
    case WSAENOTSOCK:       return CString(L"连接已取消");
    case WSAEINTR:          return CString(L"连接已取消");
    default: {
        CString s;
        s.Format(L"网络错误(代码 %d)", err);
        return s;
    }
    }
}

// ---------------- 大厅连接 ----------------
SOCKET Net_CreateListener(u_short port, CString* pErr) {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        if (pErr) *pErr = Net_DescribeError(WSAGetLastError());
        return INVALID_SOCKET;
    }
    BOOL reuse = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(ls, (const sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(ls, 2) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(ls);
        if (pErr) *pErr = Net_DescribeError(err);
        return INVALID_SOCKET;
    }
    return ls;
}

SOCKET Net_CreateClientSocket() {
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

// 主机后台线程：等待接入 -> 握手 -> 投递结果
DWORD WINAPI NetHostWorkerProc(LPVOID lp) {
    NetHostJob* job = static_cast<NetHostJob*>(lp);
    NetConnectResult* r = new NetConnectResult();
    r->ok = false;
    r->localCamp = 0;
    r->sock = INVALID_SOCKET;
    r->errorText = CString(L"等待连接被取消");

    for (;;) {
        int ready = NetWaitReadable(job->listen, job->pCancel);
        if (ready < 0) break;                       // 取消或错误
        if (ready == 0) continue;                   // 继续轮询取消标志

        SOCKET peer = accept(job->listen, nullptr, nullptr);
        if (peer == INVALID_SOCKET) break;

        NetSetRecvTimeout(peer, NET_HANDSHAKE_TIMEOUT_MS);
        NetPacketHeader hdr;
        bool handshake = false;
        if (NetRecvBytes(peer, (char*)&hdr, (int)sizeof(hdr)) && HeaderLooksValid(hdr) &&
            hdr.msgId == NET_MSG_JOIN_REQ && hdr.length == 0) {
            NetJoinAck ack;
            ack.accept = 1;                 // 同意加入
            ack.assignedCamp = 1;           // 客机执蓝方(后手)，主机执红方(先手)
            // 统一帧格式：8 字节包头(0x55AA) + 包体
            NetPacketHeader hdrAck;
            PackHeader(hdrAck, NET_MSG_JOIN_ACK, (uint32_t)sizeof(ack));
            handshake = NetSendBytes(peer, (const char*)&hdrAck, (int)sizeof(hdrAck)) &&
                        NetSendBytes(peer, (const char*)&ack, (int)sizeof(ack));
        }
        if (handshake) {
            r->ok = true;
            r->sock = peer;
            r->errorText.Empty();
            break;
        }
        closesocket(peer);                  // 握手失败，继续等待下一个客户端
    }
    PostResult(job->hwnd, r);
    delete job;
    return 0;
}

HANDLE Net_StartHostWait(HWND hwnd, SOCKET listenSock, volatile LONG* pCancel) {
    NetHostJob* job = new NetHostJob();
    job->hwnd = hwnd;
    job->listen = listenSock;
    job->pCancel = pCancel;
    DWORD tid = 0;
    return CreateThread(nullptr, 0, NetHostWorkerProc, job, 0, &tid);
}

// 客机后台线程：非阻塞 connect + select 超时 -> 握手 -> 投递结果
DWORD WINAPI NetClientWorkerProc(LPVOID lp) {
    NetClientJob* job = static_cast<NetClientJob*>(lp);
    NetConnectResult* r = new NetConnectResult();
    r->ok = false;
    r->localCamp = 1;
    r->sock = INVALID_SOCKET;
    r->errorText = CString(L"连接失败");

    SOCKET s = job->sock;
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(job->port);

    USES_CONVERSION;
    const char* ipA = T2A((LPCTSTR)job->ip);
    if (inet_pton(AF_INET, ipA, &addr.sin_addr) != 1) {
        r->errorText = CString(L"IP 地址格式不正确");
        PostResult(job->hwnd, r);   // 套接字由 UI 线程负责关闭
        delete job;
        return 0;
    }

    // 非阻塞连接，便于随时取消
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    int rc = connect(s, (const sockaddr*)&addr, sizeof(addr));
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {            // 正常：连接进行中
            rc = 0;
            while (!NetWaitWritable(s, job->pCancel)) {
                if (job->pCancel && InterlockedCompareExchange((volatile LONG*)job->pCancel, 0, 0) != 0) break;
                // 检查连接结果
            }
            int soErr = 0;
            int soLen = sizeof(soErr);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &soLen);
            rc = (soErr == 0) ? 0 : -1;
            err = soErr;
        }
        if (rc != 0) {
            r->errorText = Net_DescribeError(err);
            PostResult(job->hwnd, r);   // 套接字由 UI 线程负责关闭
            delete job;
            return 0;
        }
    }

    // 恢复阻塞模式并发起握手
    u_long blocking = 0;
    ioctlsocket(s, FIONBIO, &blocking);
    NetSetRecvTimeout(s, NET_HANDSHAKE_TIMEOUT_MS);

    NetPacketHeader hdr;
    bool handshake = false;
    NetJoinAck ack;
    // 发送空包体 JOIN_REQ
    {
        NetPacketHeader req;
        PackHeader(req, NET_MSG_JOIN_REQ, 0);
        bool snd = NetSendBytes(s, (const char*)&req, (int)sizeof(req));
        bool rcvHdr = NetRecvBytes(s, (char*)&hdr, (int)sizeof(hdr));
        bool valid = rcvHdr && HeaderLooksValid(hdr) && hdr.msgId == NET_MSG_JOIN_ACK && hdr.length == sizeof(ack);
        bool rcvBody = valid ? NetRecvBytes(s, (char*)&ack, (int)sizeof(ack)) : false;
        if (snd && rcvHdr && valid && rcvBody && ack.accept == 1) {
            handshake = true;
            r->localCamp = ack.assignedCamp;
        }
        else {
            r->errorText = CString(L"主机拒绝加入或握手失败");
        }
    }
    if (handshake) {
        r->ok = true;
        r->sock = s;
        r->errorText.Empty();
    }
    else {
        // 握手失败：套接字由 UI 线程负责关闭
    }
    PostResult(job->hwnd, r);
    delete job;
    return 0;
}

HANDLE Net_StartClientConnect(HWND hwnd, const CString& ip, u_short port, SOCKET sock, volatile LONG* pCancel) {
    NetClientJob* job = new NetClientJob();
    job->hwnd = hwnd;
    job->sock = sock;
    job->ip = ip;
    job->port = port;
    job->pCancel = pCancel;
    DWORD tid = 0;
    return CreateThread(nullptr, 0, NetClientWorkerProc, job, 0, &tid);
}

// ---------------- 对局运行期 ----------------
bool Net_SendFrame(SOCKET s, uint16_t msgId, const void* body, int bodyLen) {
    if (s == INVALID_SOCKET) return false;
    NetPacketHeader h;
    PackHeader(h, msgId, (uint32_t)bodyLen);
    if (!NetSendBytes(s, (const char*)&h, (int)sizeof(h))) return false;
    if (bodyLen > 0 && body != nullptr)
        if (!NetSendBytes(s, (const char*)body, bodyLen)) return false;
    return true;
}

DWORD WINAPI NetGameRecvProc(LPVOID lp) {
    NetGameJob* job = static_cast<NetGameJob*>(lp);
    NetPacketHeader hdr;
    char body[NET_BODY_MAX];
    for (;;) {
        if (!NetRecvBytes(job->sock, (char*)&hdr, (int)sizeof(hdr))) break;
        if (!HeaderLooksValid(hdr)) break;
        if (hdr.length > 0) {
            if (!NetRecvBytes(job->sock, body, (int)hdr.length)) break;
        }
        if (hdr.msgId == NET_MSG_MOVE_SYNC && hdr.length == sizeof(NetMoveSync)) {
            const NetMoveSync* mv = reinterpret_cast<const NetMoveSync*>(body);
            WPARAM w = (WPARAM)(((uint32_t)mv->srcIndex << 8) | mv->dstIndex);
            if (job->hwnd && ::IsWindow(job->hwnd))
                ::PostMessage(job->hwnd, WM_GAME_NET_MOVE, w, 0);
        }
        // 其他消息类型忽略
    }
    if (job->hwnd && ::IsWindow(job->hwnd))
        ::PostMessage(job->hwnd, WM_GAME_NET_CLOSED, 0, 0);
    delete job;
    return 0;
}

HANDLE Net_StartGameRecv(HWND hwnd, SOCKET s) {
    NetGameJob* job = new NetGameJob();
    job->hwnd = hwnd;
    job->sock = s;
    DWORD tid = 0;
    return CreateThread(nullptr, 0, NetGameRecvProc, job, 0, &tid);
}

void Net_CloseSession(SOCKET& s, HANDLE& recvThread) {
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);   // 唤醒阻塞中的 recv
        closesocket(s);
        s = INVALID_SOCKET;
    }
    if (recvThread != nullptr) {
        WaitForSingleObject(recvThread, 2000);
        CloseHandle(recvThread);
        recvThread = nullptr;
    }
}