// LobbyDlg.cpp : 大厅界面实现（控件在代码中动态创建，布局自适应当前客户区尺寸）
#include "pch.h"
#include "framework.h"
#include "animalChess.h"
#include "LobbyDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static const wchar_t* DIFF_NAMES[3] = { L"简单", L"中等", L"困难" };

CLobbyDlg::CLobbyDlg(GameLaunchInfo* pLaunch, CWnd* pParent)
    : CDialogEx(IDD_LOBBY_DIALOG, pParent)
    , m_pLaunch(pLaunch)
    , m_modeAi(true)
    , m_aiLevel(2)
    , m_bHost(true)
    , m_netCamp(0)
    , m_busy(false)
    , m_connected(false)
    , m_transferred(false)
    , m_listenSock(INVALID_SOCKET)
    , m_sock(INVALID_SOCKET)
    , m_workerThread(nullptr)
{
    m_cancel = 0;
    if (m_pLaunch) {   // 记忆上次选择
        m_modeAi = (m_pLaunch->mode == GAME_MODE_AI);
        m_aiLevel = m_pLaunch->aiLevel;
        if (m_aiLevel < 1 || m_aiLevel > 3) m_aiLevel = 2;
        m_bHost = m_pLaunch->netIsHost;
    }
    m_ipText = L"127.0.0.1";
    m_portText = L"9002";
}

CLobbyDlg::~CLobbyDlg()
{
}

void CLobbyDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CLobbyDlg, CDialogEx)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_LB_MODE_AI, &CLobbyDlg::OnBnClickedModeAi)
    ON_BN_CLICKED(IDC_LB_MODE_NET, &CLobbyDlg::OnBnClickedModeNet)
    ON_BN_CLICKED(IDC_LB_DIFF_EASY, &CLobbyDlg::OnBnClickedDiffEasy)
    ON_BN_CLICKED(IDC_LB_DIFF_MID, &CLobbyDlg::OnBnClickedDiffMid)
    ON_BN_CLICKED(IDC_LB_DIFF_HARD, &CLobbyDlg::OnBnClickedDiffHard)
    ON_BN_CLICKED(IDC_LB_NET_HOST, &CLobbyDlg::OnBnClickedNetHost)
    ON_BN_CLICKED(IDC_LB_NET_CLIENT, &CLobbyDlg::OnBnClickedNetClient)
    ON_BN_CLICKED(IDC_LB_CONNECT, &CLobbyDlg::OnBnClickedConnect)
    ON_BN_CLICKED(IDC_LB_CONNECT_CANCEL, &CLobbyDlg::OnBnClickedConnectCancel)
    ON_BN_CLICKED(IDC_LB_START, &CLobbyDlg::OnBnClickedStart)
    ON_BN_CLICKED(IDC_LB_QUIT, &CLobbyDlg::OnBnClickedQuit)
    ON_MESSAGE(WM_LOBBY_NET_CONNECTED, &CLobbyDlg::OnNetConnected)
    ON_MESSAGE(WM_LOBBY_NET_FAILED, &CLobbyDlg::OnNetFailed)
END_MESSAGE_MAP()

// ---------------------------------------------------------------- 字体辅助
static void MakeFont(CFont& f, int px, int weight, const wchar_t* face)
{
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    f.DeleteObject();
    f.CreateFontIndirect(&lf);
}

void CLobbyDlg::ApplyFont(CWnd* pWnd, CFont* pFont)
{
    if (pWnd && pWnd->GetSafeHwnd()) pWnd->SetFont(pFont);
}

// ---------------------------------------------------------------- 控件搭建
void CLobbyDlg::CreateControls()
{
    CRect rc;
    GetClientRect(&rc);
    const int W = rc.Width();
    const int H = rc.Height();

    // 标题
    m_stcTitle.Create(L"斗兽棋 · 游戏大厅", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(0, 0, 10, 10), this, 0x79F0);

    // ---- 模式单选 ----
    m_btnModeAi.Create(L"人机对战", WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_MODE_AI);
    m_btnModeNet.Create(L"联机对战", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_MODE_NET);

    // ---- 人机组 ----
    m_grpAi.Create(L"人机对战设置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, CRect(0, 0, 10, 10), this, 0x79F1);
    m_btnDiffEasy.Create(L"简单", WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_DIFF_EASY);
    m_btnDiffMid.Create(L"中等", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_DIFF_MID);
    m_btnDiffHard.Create(L"困难", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_DIFF_HARD);
    m_stcAiNote.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, IDC_LB_STATIC_HINT1);

    // ---- 联机组 ----
    m_grpNet.Create(L"联机对战设置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, CRect(0, 0, 10, 10), this, 0x79F2);
    m_btnNetHost.Create(L"创建房间（主机 · 执红先手）", WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_NET_HOST);
    m_btnNetClient.Create(L"加入房间（客机 · 执蓝后手）", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_NET_CLIENT);
    m_stcIpLabel.Create(L"对端 IP：", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, 0x79F3);
    m_stcPortLabel.Create(L"端口：", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, 0x79F4);
    m_editIp.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_LB_IP_EDIT);
    m_editPort.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, CRect(0, 0, 10, 10), this, IDC_LB_PORT_EDIT);
    m_btnConnect.Create(L"连接", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_CONNECT);
    m_btnConnectCancel.Create(L"取消", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_CONNECT_CANCEL);
    m_stcNetNote.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, IDC_LB_STATIC_HINT2);
    m_stcStatus.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, IDC_LB_STATIC_STATUS);

    // ---- 底部按钮 ----
    m_btnStart.Create(L"开始对战", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_START);
    m_btnQuit.Create(L"退出游戏", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_LB_QUIT);

    // 字体
    MakeFont(m_fontTitle, 30, FW_BOLD, L"微软雅黑");
    MakeFont(m_fontGroup, 14, FW_BOLD, L"微软雅黑");
    MakeFont(m_fontNormal, 15, FW_NORMAL, L"微软雅黑");
    MakeFont(m_fontSmall, 13, FW_NORMAL, L"微软雅黑");

    ApplyFont(&m_stcTitle, &m_fontTitle);
    ApplyFont(&m_btnModeAi, &m_fontNormal);
    ApplyFont(&m_btnModeNet, &m_fontNormal);
    ApplyFont(&m_grpAi, &m_fontGroup);
    ApplyFont(&m_btnDiffEasy, &m_fontNormal);
    ApplyFont(&m_btnDiffMid, &m_fontNormal);
    ApplyFont(&m_btnDiffHard, &m_fontNormal);
    ApplyFont(&m_stcAiNote, &m_fontSmall);
    ApplyFont(&m_grpNet, &m_fontGroup);
    ApplyFont(&m_btnNetHost, &m_fontNormal);
    ApplyFont(&m_btnNetClient, &m_fontNormal);
    ApplyFont(&m_stcIpLabel, &m_fontSmall);
    ApplyFont(&m_stcPortLabel, &m_fontSmall);
    ApplyFont(&m_editIp, &m_fontNormal);
    ApplyFont(&m_editPort, &m_fontNormal);
    ApplyFont(&m_btnConnect, &m_fontNormal);
    ApplyFont(&m_btnConnectCancel, &m_fontNormal);
    ApplyFont(&m_stcNetNote, &m_fontSmall);
    ApplyFont(&m_stcStatus, &m_fontNormal);
    ApplyFont(&m_btnStart, &m_fontNormal);
    ApplyFont(&m_btnQuit, &m_fontNormal);
}

void CLobbyDlg::LayoutByClient()
{
    CRect rc;
    GetClientRect(&rc);
    const int W = rc.Width();
    const int H = rc.Height();

    int x0 = 24;                 // 左边距
    int wg = W - x0 * 2;         // 分组宽

    // 标题
    m_stcTitle.MoveWindow(x0, 10, wg, 40);

    // 模式选择行
    m_btnModeAi.MoveWindow(x0 + 8, 58, 150, 28);
    m_btnModeNet.MoveWindow(x0 + 168, 58, 150, 28);

    // 人机组
    m_grpAi.MoveWindow(x0, 94, wg, 122);
    m_btnDiffEasy.MoveWindow(x0 + 26, 128, 92, 28);
    m_btnDiffMid.MoveWindow(x0 + 124, 128, 92, 28);
    m_btnDiffHard.MoveWindow(x0 + 222, 128, 92, 28);
    m_stcAiNote.MoveWindow(x0 + 26, 164, wg - 52, 44);

    // 联机组
    m_grpNet.MoveWindow(x0, 94, wg, 160);
    m_btnNetHost.MoveWindow(x0 + 26, 122, wg - 52, 28);
    m_btnNetClient.MoveWindow(x0 + 26, 150, wg - 52, 28);
    m_stcIpLabel.MoveWindow(x0 + 26, 184, 64, 24);
    m_editIp.MoveWindow(x0 + 92, 180, 158, 26);
    m_stcPortLabel.MoveWindow(x0 + 262, 184, 40, 24);
    m_editPort.MoveWindow(x0 + 304, 180, 70, 26);
    m_btnConnect.MoveWindow(x0 + 386, 178, 96, 30);
    m_btnConnectCancel.MoveWindow(x0 + 488, 178, 76, 30);
    m_stcNetNote.MoveWindow(x0 + 26, 212, wg - 52, 20);
    m_stcStatus.MoveWindow(x0 + 26, 234, wg - 52, 24);

    // 底部操作按钮
    int bw = 150, bh = 38;
    int by = H - bh - 26;
    m_btnStart.MoveWindow(W / 2 - bw - 12, by, bw, bh);
    m_btnQuit.MoveWindow(W / 2 + 12, by, bw, bh);
}

BOOL CLobbyDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    HICON hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    SetIcon(hIcon, TRUE);
    SetIcon(hIcon, FALSE);

    // 固定窗口尺寸，保证布局按设计宽度渲染
    SetWindowPos(nullptr, 0, 0, 700, 600, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    CreateControls();
    LayoutByClient();

    // 依据上次选择恢复控件状态
    m_btnModeAi.SetCheck(m_modeAi ? BST_CHECKED : BST_UNCHECKED);
    m_btnModeNet.SetCheck(m_modeAi ? BST_UNCHECKED : BST_CHECKED);
    m_btnDiffEasy.SetCheck(m_aiLevel == 1 ? BST_CHECKED : BST_UNCHECKED);
    m_btnDiffMid.SetCheck(m_aiLevel == 2 ? BST_CHECKED : BST_UNCHECKED);
    m_btnDiffHard.SetCheck(m_aiLevel == 3 ? BST_CHECKED : BST_UNCHECKED);
    m_btnNetHost.SetCheck(m_bHost ? BST_CHECKED : BST_UNCHECKED);
    m_btnNetClient.SetCheck(m_bHost ? BST_UNCHECKED : BST_CHECKED);
    m_editIp.SetWindowText(m_ipText);
    m_editPort.SetWindowText(m_portText);

    UpdateModeUI();
    return TRUE;
}

void CLobbyDlg::SetStatusText(const CString& text, bool warn)
{
    m_statusText = text;
    if (m_stcStatus.GetSafeHwnd())
        m_stcStatus.SetWindowText(text);
}

// ---------------------------------------------------------------- 模式切换
void CLobbyDlg::UpdateModeUI()
{
    bool ai = m_modeAi;

    m_grpAi.ShowWindow(ai ? SW_SHOW : SW_HIDE);
    m_btnDiffEasy.ShowWindow(ai ? SW_SHOW : SW_HIDE);
    m_btnDiffMid.ShowWindow(ai ? SW_SHOW : SW_HIDE);
    m_btnDiffHard.ShowWindow(ai ? SW_SHOW : SW_HIDE);
    m_stcAiNote.ShowWindow(ai ? SW_SHOW : SW_HIDE);

    m_grpNet.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_btnNetHost.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_btnNetClient.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_stcIpLabel.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_stcPortLabel.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_editIp.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_editPort.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_btnConnect.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_btnConnectCancel.ShowWindow(ai ? SW_HIDE : (m_busy ? SW_SHOW : SW_HIDE));
    m_stcNetNote.ShowWindow(ai ? SW_HIDE : SW_SHOW);
    m_stcStatus.ShowWindow(ai ? SW_HIDE : SW_SHOW);

    // 联机组控件可用性
    BOOL netIdle = !ai && !m_busy && !m_connected;
    m_btnNetHost.EnableWindow(netIdle);
    m_btnNetClient.EnableWindow(netIdle);
    m_editIp.EnableWindow(netIdle && !m_bHost);
    m_stcIpLabel.EnableWindow(netIdle && !m_bHost);
    m_editPort.EnableWindow(netIdle);
    m_btnConnect.EnableWindow(!ai && !m_busy);
    if (!ai) {
        if (m_busy)
            m_btnConnect.SetWindowText(L"连接中…");
        else if (m_connected)
            m_btnConnect.SetWindowText(L"断开连接");
        else
            m_btnConnect.SetWindowText(L"连接");

        // 提示与状态
        if (!m_connected && !m_busy) {
            if (m_bHost) {
                m_stcNetNote.SetWindowText(L"提示：请把本机 IP 告知对方，并等待对方输入后点击【连接】；首次运行请放行防火墙。");
                if (m_statusText.IsEmpty()) SetStatusText(L"未连接：等待建立会话");
            }
            else {
                m_stcNetNote.SetWindowText(L"提示：在下方填写主机 IP 与端口后点击【连接】。");
                if (m_statusText.IsEmpty()) SetStatusText(L"未连接：等待建立会话");
            }
        }
        else if (m_connected) {
            m_stcNetNote.SetWindowText(L"连接已建立，点击【开始对战】进入棋盘。");
        }
    }
    else {
        m_stcAiNote.SetWindowText(L"本局你执红方（先手），电脑执蓝方（后手）；难度=搜索深度：简单<中等<困难。");
    }

    // 开始按钮
    m_btnStart.EnableWindow(ai || m_connected);
    // 底部退出按钮始终可用
}

void CLobbyDlg::OnBnClickedModeAi()
{
    if (m_modeAi) return;
    if (!m_busy && m_connected) CloseNetSession(false);
    m_modeAi = true;
    UpdateModeUI();
}

void CLobbyDlg::OnBnClickedModeNet()
{
    if (!m_modeAi) return;
    m_modeAi = false;
    UpdateModeUI();
}

void CLobbyDlg::OnBnClickedDiffEasy()  { m_aiLevel = 1; }
void CLobbyDlg::OnBnClickedDiffMid()   { m_aiLevel = 2; }
void CLobbyDlg::OnBnClickedDiffHard()  { m_aiLevel = 3; }

void CLobbyDlg::OnBnClickedNetHost()
{
    if (m_bHost) return;
    m_bHost = true;
    SetStatusText(CString());
    UpdateModeUI();
}

void CLobbyDlg::OnBnClickedNetClient()
{
    if (!m_bHost) return;
    m_bHost = false;
    SetStatusText(CString());
    UpdateModeUI();
}

// ---------------------------------------------------------------- 连接管理
bool CLobbyDlg::GetPort(u_short& port)
{
    CString t;
    m_editPort.GetWindowText(t);
    t.Trim();
    int p = _wtoi(t);
    if (p < 1 || p > 65535) {
        SetStatusText(L"端口无效：请输入 1~65535 之间的数字");
        return false;
    }
    port = (u_short)p;
    return true;
}

void CLobbyDlg::StartConnect()
{
    m_editIp.GetWindowText(m_ipText);
    m_editPort.GetWindowText(m_portText);

    u_short port = 0;
    if (!GetPort(port)) return;

    if (m_bHost) {
        CString err;
        m_listenSock = Net_CreateListener(port, &err);
        if (m_listenSock == INVALID_SOCKET) {
            SetStatusText(L"创建监听失败：" + err);
            return;
        }
        m_cancel = 0;
        m_workerThread = Net_StartHostWait(m_hWnd, m_listenSock, &m_cancel);
        m_busy = true;
        CString s;
        s.Format(L"正在监听端口 %u，等待对方加入……", (unsigned)port);
        SetStatusText(s);
    }
    else {
        m_sock = Net_CreateClientSocket();
        if (m_sock == INVALID_SOCKET) {
            SetStatusText(L"创建套接字失败");
            return;
        }
        m_cancel = 0;
        m_workerThread = Net_StartClientConnect(m_hWnd, m_ipText, port, m_sock, &m_cancel);
        m_busy = true;
        CString s;
        s.Format(L"正在连接 %s:%u ……", (LPCTSTR)m_ipText, (unsigned)port);
        SetStatusText(s);
    }
    UpdateModeUI();
}

void CLobbyDlg::CloseNetSession(bool showText)
{
    if (m_transferred) return;    // 连接已交给对局窗口，不得关闭

    if (m_busy) {
        InterlockedExchange(&m_cancel, 1);
        if (m_workerThread) {
            WaitForSingleObject(m_workerThread, 2500);
            CloseHandle(m_workerThread);
            m_workerThread = nullptr;
        }
        m_busy = false;
    }
    if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    if (m_listenSock != INVALID_SOCKET) { closesocket(m_listenSock); m_listenSock = INVALID_SOCKET; }
    m_connected = false;
    m_cancel = 0;
    if (m_workerThread) { CloseHandle(m_workerThread); m_workerThread = nullptr; }
    if (showText) SetStatusText(L"已断开连接");
    else if (!m_modeAi) SetStatusText(CString());
}

void CLobbyDlg::OnBnClickedConnect()
{
    if (m_busy) return;
    if (m_connected) {
        CloseNetSession(true);
        UpdateModeUI();
        return;
    }
    StartConnect();
}

void CLobbyDlg::OnBnClickedConnectCancel()
{
    if (!m_busy) return;
    CloseNetSession(false);
    SetStatusText(L"已取消连接");
    UpdateModeUI();
}

LRESULT CLobbyDlg::OnNetConnected(WPARAM, LPARAM lParam)
{
    NetConnectResult* r = reinterpret_cast<NetConnectResult*>(lParam);
    if (!r) return 0;

    if (!m_busy) {
        // 已被用户取消/关闭，丢弃连接
        if (r->sock != INVALID_SOCKET) closesocket(r->sock);
        delete r;
        return 0;
    }
    m_busy = false;
    m_cancel = 0;
    m_sock = r->sock;
    m_connected = true;
    m_netCamp = (r->ok && (r->localCamp == 0 || r->localCamp == 1)) ? r->localCamp : (m_bHost ? 0 : 1);

    if (m_workerThread) { WaitForSingleObject(m_workerThread, 1000); CloseHandle(m_workerThread); m_workerThread = nullptr; }

    CString s;
    if (m_netCamp == 0)
        s.Format(L"连接成功！你是红方（先手）。%s", m_bHost ? L"(主机)" : L"(客机)");
    else
        s.Format(L"连接成功！你是蓝方（后手）。%s", m_bHost ? L"(主机)" : L"(客机)");
    SetStatusText(s);
    UpdateModeUI();

    delete r;
    return 0;
}

LRESULT CLobbyDlg::OnNetFailed(WPARAM, LPARAM lParam)
{
    NetConnectResult* r = reinterpret_cast<NetConnectResult*>(lParam);
    if (!r) return 0;

    if (!m_busy) {   // 用户主动取消后到达的迟到消息
        if (r->sock != INVALID_SOCKET) closesocket(r->sock);
        delete r;
        return 0;
    }
    m_busy = false;
    bool wasCanceled = (m_cancel != 0);
    m_cancel = 0;

    if (m_workerThread) { WaitForSingleObject(m_workerThread, 1000); CloseHandle(m_workerThread); m_workerThread = nullptr; }
    if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    if (m_listenSock != INVALID_SOCKET) { closesocket(m_listenSock); m_listenSock = INVALID_SOCKET; }

    if (wasCanceled)
        SetStatusText(L"已取消连接");
    else
        SetStatusText(L"连接失败：" + (r ? r->errorText : CString(L"未知错误")));

    UpdateModeUI();
    delete r;
    return 0;
}

// ---------------------------------------------------------------- 确定/取消
void CLobbyDlg::WriteBackLaunch()
{
    if (!m_pLaunch) return;
    m_pLaunch->mode = m_modeAi ? GAME_MODE_AI : GAME_MODE_NET;
    m_pLaunch->aiLevel = m_aiLevel;
    m_pLaunch->netIsHost = m_bHost;
    m_pLaunch->netLocalCamp = m_netCamp;
    m_pLaunch->netSocket = (m_connected && !m_transferred) ? m_sock : INVALID_SOCKET;
}

void CLobbyDlg::OnOK()
{
    if (m_modeAi) {
        WriteBackLaunch();
        CDialogEx::EndDialog(IDOK);
        return;
    }
    if (!m_connected) {
        SetStatusText(L"请先完成联机连接（主机等待客机加入 / 客机输入主机地址）");
        UpdateModeUI();
        return;
    }
    WriteBackLaunch();
    m_transferred = true;          // 把连接套接字所有权交给对局窗口
    if (m_listenSock != INVALID_SOCKET) { closesocket(m_listenSock); m_listenSock = INVALID_SOCKET; }
    if (m_workerThread) { CloseHandle(m_workerThread); m_workerThread = nullptr; }
    CDialogEx::EndDialog(IDOK);
}

void CLobbyDlg::OnCancel()
{
    CloseNetSession(false);
    CDialogEx::EndDialog(IDCANCEL);
}

void CLobbyDlg::OnBnClickedStart() { OnOK(); }
void CLobbyDlg::OnBnClickedQuit()  { OnCancel(); }

void CLobbyDlg::OnDestroy()
{
    CloseNetSession(false);
    CDialogEx::OnDestroy();
}
