// AnimalChessNet.h : 斗兽棋“联机对战”通信模块（MFC ↔ MFC，TCP / 局域网）
//
// 与仓库顶层 README《斗兽棋极简通信协议 · 协议二：双机联机对战通信》保持一致：
//   - 统一小包：Magic(2B)=0x55,0xAA + MsgID(2B) + Length(4B) + Body
//   - 握手：NET_JOIN_REQ(Guest->Host) / NET_JOIN_ACK(Host->Guest)
//   - 走子同步：NET_MOVE_SYNC(双向)
// 本实现约定：主机(Host)执红方先手，客机(Guest)执蓝方后手；
// 双方各自维护一份“确定性规则”的权威棋盘，走子消息到达后按相同规则校验落子。
#pragma once

#include <cstdint>

// Winsock2 必须在任何 windows.h 之前引入，MFC 框架头由 pch.h 统一包含，
// 因此本头文件可直接使用 SOCKET 等类型。
#include <winsock2.h>
#include <ws2tcpip.h>

// ---- 消息号（与 README 一致）----
enum NetMsgId {
    NET_MSG_JOIN_REQ   = 0x0010,   // 客机 -> 主机：请求加入
    NET_MSG_JOIN_ACK   = 0x0011,   // 主机 -> 客机：同意/拒绝 + 分配阵营
    NET_MSG_MOVE_SYNC  = 0x0012,   // 双向：同步一次走子(src -> dst)
};

#pragma pack(push, 1)
struct NetPacketHeader {
    uint8_t  magic[2];             // 0x55 0xAA
    uint16_t msgId;                // NetMsgId
    uint32_t length;               // Body 字节数
};

struct NetJoinAck {
    uint8_t accept;                // 1:同意加入 0:拒绝
    uint8_t assignedCamp;          // 0:红方/先手 1:蓝方/后手
};

struct NetMoveSync {
    uint8_t srcIndex;              // 起点 (0~62)
    uint8_t dstIndex;              // 终点 (0~62)
};
#pragma pack(pop)

// ---- 工作线程 -> UI 线程的自定义消息 ----
// 大厅使用：
#define WM_LOBBY_NET_CONNECTED   (WM_APP + 10)   // lParam = NetConnectResult*（堆上，处理方负责 delete）
#define WM_LOBBY_NET_FAILED      (WM_APP + 11)   // lParam = NetConnectResult*（同上）
// 对局窗口使用：
#define WM_GAME_NET_MOVE         (WM_APP + 12)   // wParam = (srcIndex << 8) | dstIndex
#define WM_GAME_NET_CLOSED       (WM_APP + 13)   // 对端断开 / 网络错误

struct NetConnectResult {
    bool     ok;                  // 是否连接成功
    int      localCamp;           // 成功后本机分配到的阵营：0 红 / 1 蓝
    SOCKET   sock;                // 成功时为已建立的 TCP 套接字
    CString  errorText;           // 失败原因（中文描述）
};

// ---------- 基础 ----------
bool    Net_Startup();            // WSAStartup（可在 App 启动时调用一次）
void    Net_Cleanup();            // WSACleanup
CString Net_DescribeError(int err);

// ---------- 大厅连接（工作线程内部阻塞 + select 超时，UI 用“取消标志”终止）----------
// 主机：创建监听套接字（返回 INVALID_SOCKET 表示失败，*pErr 给出原因）
SOCKET  Net_CreateListener(u_short port, CString* pErr);
// 主机：后台等待一个客户端接入，成功后回发 NET_JOIN_ACK(camp=1 蓝方)，
//       并向 hwnd 投递 WM_LOBBY_NET_CONNECTED；失败投递 WM_LOBBY_NET_FAILED。
HANDLE  Net_StartHostWait(HWND hwnd, SOCKET listenSock, volatile LONG* pCancel);
// 客机：后台连接 ip:port 并发起握手，结果同样投递到 hwnd。
// sock：由调用方先 Net_CreateClientSocket() 创建（用于支持中途取消），
//       连接/握手失败后套接字仍归调用方所有、由其负责关闭。
HANDLE  Net_StartClientConnect(HWND hwnd, const CString& ip, u_short port, SOCKET sock, volatile LONG* pCancel);
// 客机连接前置：创建未连接 TCP 套接字（供取消时直接 close）
SOCKET  Net_CreateClientSocket();

// ---------- 对局运行期 ----------
// 启动接收线程：循环读取远端消息；走子投递 WM_GAME_NET_MOVE，
// 连接断开/出错投递 WM_GAME_NET_CLOSED 后线程自行退出。
HANDLE  Net_StartGameRecv(HWND hwnd, SOCKET s);
// 发送一帧（阻塞短写，最多数十字节）
bool    Net_SendFrame(SOCKET s, uint16_t msgId, const void* body, int bodyLen);
// 主动结束对局连接（唤醒接收线程并回收）
void    Net_CloseSession(SOCKET& s, HANDLE& recvThread);
