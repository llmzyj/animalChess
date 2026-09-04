// LobbyDlg.h : 游戏大厅对话框（游戏入口）
//
// 功能：
//   1) 人机对战：可选难度 简单/中等/困难（玩家执红方先手，电脑执蓝方）
//   2) 联机对战：创建房间(主机) 或 加入房间(客机)
//      - 主机：监听等待客机接入，执红方先手
//      - 客机：输入主机 IP/端口并连接，执蓝方后手
//   连接建立后点击 [开始对战] 进入对局窗口。
#pragma once

#include "AnimalChessNet.h"

// ---- 游戏模式 ----
enum GameMode {
    GAME_MODE_AI  = 1,
    GAME_MODE_NET = 2,
};

// 大厅 -> 对局窗口 的启动参数
struct GameLaunchInfo {
    int    mode;            // GAME_MODE_AI / GAME_MODE_NET
    int    aiLevel;         // 1 简单 / 2 中等 / 3 困难（人机模式）
    bool   netIsHost;       // 联机模式：是否为主机
    int    netLocalCamp;    // 联机模式：本机阵营 0 红 / 1 蓝
    SOCKET netSocket;       // 联机模式：已建立的 TCP 连接（所有权移交对局窗口）
};

// ---- 大厅内控件 ID（动态创建，取值避开系统命令区）----
enum LobbyCtrlId {
    IDC_LB_MODE_AI       = 0x7101,
    IDC_LB_MODE_NET      = 0x7102,
    IDC_LB_DIFF_EASY     = 0x7103,
    IDC_LB_DIFF_MID      = 0x7104,
    IDC_LB_DIFF_HARD     = 0x7105,
    IDC_LB_NET_HOST      = 0x7106,
    IDC_LB_NET_CLIENT    = 0x7107,
    IDC_LB_IP_EDIT       = 0x7108,
    IDC_LB_PORT_EDIT     = 0x7109,
    IDC_LB_CONNECT       = 0x710A,
    IDC_LB_CONNECT_CANCEL = 0x710B,
    IDC_LB_START         = 0x710C,
    IDC_LB_QUIT          = 0x710D,
    IDC_LB_STATIC_HINT1  = 0x710E,  // 人机设置提示
    IDC_LB_STATIC_HINT2  = 0x710F,  // 联机设置提示
    IDC_LB_STATIC_STATUS = 0x7110,  // 联机状态
};

class CLobbyDlg : public CDialogEx
{
public:
    // pLaunch：指向 App 持有的参数对象；确定(开始对战)时回写本次选择
    explicit CLobbyDlg(GameLaunchInfo* pLaunch, CWnd* pParent = nullptr);
    virtual ~CLobbyDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_LOBBY_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();          // Enter / [开始对战]
    virtual void OnCancel();      // Esc / 关闭

    afx_msg void OnDestroy();
    afx_msg void OnBnClickedModeAi();
    afx_msg void OnBnClickedModeNet();
    afx_msg void OnBnClickedDiffEasy();
    afx_msg void OnBnClickedDiffMid();
    afx_msg void OnBnClickedDiffHard();
    afx_msg void OnBnClickedNetHost();
    afx_msg void OnBnClickedNetClient();
    afx_msg void OnBnClickedConnect();
    afx_msg void OnBnClickedConnectCancel();
    afx_msg void OnBnClickedStart();
    afx_msg void OnBnClickedQuit();
    afx_msg LRESULT OnNetConnected(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNetFailed(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    // 界面搭建与状态
    void CreateControls();
    void ApplyFont(CWnd* pWnd, CFont* pFont);
    void LayoutByClient();
    void UpdateModeUI();      // 根据当前模式/连接状态刷新控件可用性
    void SetStatusText(const CString& text, bool warn = false);

    // 网络会话管理
    void StartConnect();
    void CancelConnect(bool showText);
    void CloseNetSession(bool showText);
    bool GetPort(u_short& port);

    void WriteBackLaunch();   // 收集结果到 m_pLaunch

    CFont m_fontTitle;        // 标题
    CFont m_fontGroup;        // 分组/按钮文字
    CFont m_fontNormal;       // 常规文字
    CFont m_fontSmall;        // 提示文字

    CStatic m_stcTitle;       // 大厅标题
    CButton m_grpAi;          // “人机对战”设置分组框
    CButton m_grpNet;         // “联机对战”设置分组框
    CButton m_btnModeAi;
    CButton m_btnModeNet;
    CButton m_btnDiffEasy;
    CButton m_btnDiffMid;
    CButton m_btnDiffHard;
    CButton m_btnNetHost;
    CButton m_btnNetClient;
    CStatic m_stcAiNote;      // “玩家执红…”提示
    CStatic m_stcIpLabel;
    CStatic m_stcPortLabel;
    CEdit   m_editIp;
    CEdit   m_editPort;
    CButton m_btnConnect;
    CButton m_btnConnectCancel;
    CStatic m_stcNetNote;     // 联机提示（host/client 说明）
    CStatic m_stcStatus;
    CButton m_btnStart;
    CButton m_btnQuit;

    GameLaunchInfo* m_pLaunch;   // 不拥有

    bool   m_modeAi;         // true 人机 / false 联机
    int    m_aiLevel;        // 1..3
    bool   m_bHost;          // 联机角色
    int    m_netCamp;        // 联机连接成功后由主机分配：0 红 / 1 蓝
    CString m_ipText;
    CString m_portText;
    CString m_statusText;    // 联机状态行文字

    // 网络状态机：空闲 -> 连接中 -> 已连接
    bool   m_busy;
    bool   m_connected;
    bool   m_transferred;    // 连接是否已移交给对局窗口
    SOCKET m_listenSock;     // 主机监听套接字
    SOCKET m_sock;           // 已建立（或正在建立）的连接套接字
    HANDLE m_workerThread;   // 大厅连接线程
    volatile LONG m_cancel;
};
