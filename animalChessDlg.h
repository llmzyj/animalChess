// animalChessDlg.h : 对局窗口（棋盘渲染 + 人机/联机交互）
//
// 模式感知：
//   - 人机对战：本机玩家固定执红方(先手)，点击己方棋子选子、点目标格落子，
//     落子后通过 Engine_TriggerAiAsync 在后台触发电脑(蓝方)思考；
//   - 联机对战：主机执红先手、客机执蓝后手。本机落子前先经 Engine_TryMove
//     本地仲裁，成功后把 NET_MOVE_SYNC 发给对端，对端按相同规则落子，保证
//     双方棋盘一致；对端走子由后台接收线程投递 WM_GAME_NET_MOVE 处理。
#pragma once
#include "engine.h"
#include "LobbyDlg.h"

class CanimalChessDlg : public CDialogEx
{
public:
    CanimalChessDlg(CWnd* pParent = nullptr);

    // 大厅进入对局前注入模式参数
    void SetLaunchInfo(const GameLaunchInfo& info) { m_launch = info; }

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_ANIMALCHESS_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnDestroy();

    HICON m_hIcon;
    int m_selectedIdx;

    CButton m_lanGroup;
    CStatic m_addressLabel;
    CEdit m_addressEdit;
    CStatic m_portLabel;
    CEdit m_portEdit;
    CButton m_localAiButton;
    CButton m_hostButton;
    CButton m_joinButton;
    CButton m_disconnectButton;
    CStatic m_networkStatus;

    LanSession m_lanSession;
    GameMode m_gameMode;
    bool m_waitingForGuestConfirmation;
    uint8_t m_pendingSrc;
    uint8_t m_pendingDst;

    void DrawMoveArrow(CDC* pDC, CPoint ptStart, CPoint ptEnd);
    void StartNewGame();                    // 引擎复位 + UI 复位
    bool IsMyColor(uint8_t pc) const;       // pc 是否属于本机阵营
    bool IsMyTurn(const MsgBoardSnapshot& snap) const;
    void OnLocalMoveDone(uint8_t srcIdx, uint8_t dstIdx);   // 本机成功落子后的后续
    void HandlePeerGone();                  // 对端断开：提示并返回大厅
    const wchar_t* DifficultyName() const;

    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedRestart();
    afx_msg void OnBnClickedBackLobby();
    afx_msg LRESULT OnNetMove(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNetClosed(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    enum {
        IDC_GAME_RESTART = 0x6301,   // 重新开局（仅人机模式）
        IDC_GAME_BACK = 0x6302,   // 返回大厅
        GAME_TIMER_AI = 0x11,     // AI 思考轮询定时器
    };

    GameLaunchInfo m_launch;
    bool   m_aiMode;           // true: 人机  false: 联机
    int    m_myCamp;           // 本机阵营 0 红 / 1 蓝
    int    m_aiLevel;          // 人机难度 1..3

    CButton m_btnRestart;
    CButton m_btnBack;
    CFont   m_uiFont;

    // 联机会话
    SOCKET m_netSock;
    HANDLE m_netRecvThread;
    bool   m_netClosing;       // 防止断开提示重复触发
};
