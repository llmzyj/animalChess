// animalChessDlg.cpp : 对局窗口实现（详见 animalChessDlg.h）
#include "pch.h"
#include "framework.h"
#include "animalChess.h"
#include "animalChessDlg.h"
#include "AnimalChessNet.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 棋盘绘制常量（与点击坐标换算保持一致）
static const int GRID_SIZE = 65;
static const int OFFSET_X = 30;
static const int OFFSET_Y = 40;

static const wchar_t* AI_LEVEL_NAMES[3] = { L"简单", L"中等", L"困难" };

static CString PieceNameW(uint8_t pc)
{
    const char* s = Engine_GetPieceName(pc);
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return CString();
    CString out;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.GetBuffer(n), n);
    out.ReleaseBuffer();
    return out;
}

CanimalChessDlg::CanimalChessDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_ANIMALCHESS_DIALOG, pParent)
    , m_selectedIdx(-1)
    , m_aiMode(true)
    , m_myCamp(0)
    , m_aiLevel(2)
    , m_netSock(INVALID_SOCKET)
    , m_netRecvThread(nullptr)
    , m_netClosing(false)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

    // 默认参数：人机/中等（实际会在 SetLaunchInfo 中被大厅覆盖）
    m_launch.mode = GAME_MODE_AI;
    m_launch.aiLevel = 2;
    m_launch.netIsHost = true;
    m_launch.netLocalCamp = 0;
    m_launch.netSocket = INVALID_SOCKET;
}

void CanimalChessDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CanimalChessDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_LBUTTONDOWN()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_GAME_RESTART, &CanimalChessDlg::OnBnClickedRestart)
    ON_BN_CLICKED(IDC_GAME_BACK, &CanimalChessDlg::OnBnClickedBackLobby)
    ON_MESSAGE(WM_GAME_NET_MOVE, &CanimalChessDlg::OnNetMove)
    ON_MESSAGE(WM_GAME_NET_CLOSED, &CanimalChessDlg::OnNetClosed)
END_MESSAGE_MAP()

const wchar_t* CanimalChessDlg::DifficultyName() const
{
    int idx = m_aiLevel - 1;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    return AI_LEVEL_NAMES[idx];
}

bool CanimalChessDlg::IsMyColor(uint8_t pc) const
{
    if (m_myCamp == 0) return pc >= 1 && pc <= 8;      // 红方棋子 1~8
    return pc >= 17 && pc <= 24;                       // 蓝方棋子 17~24
}

bool CanimalChessDlg::IsMyTurn(const MsgBoardSnapshot& snap) const
{
    if (snap.gameStatus != 0) return false;
    if (snap.currentTurn != m_myCamp) return false;
    if (m_aiMode && Engine_IsAiThinking()) return false;   // 电脑思考中不允许抢先操作
    return true;
}

BOOL CanimalChessDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // 棋盘由父窗口双缓冲绘制，排除子控件区域可避免状态刷新时覆盖右侧控件。
    ModifyStyle(0, WS_CLIPCHILDREN);
    SetWindowPos(NULL, 0, 0, 930, 560, SWP_NOMOVE | SWP_NOZORDER);

    // ---------- 记录本局模式参数 ----------
    m_aiMode = (m_launch.mode == GAME_MODE_AI);
    m_aiLevel = m_launch.aiLevel;
    if (m_aiLevel < 1 || m_aiLevel > 3) m_aiLevel = 2;
    m_myCamp = (m_aiMode ? 0 : m_launch.netLocalCamp);
    if (m_myCamp != 0 && m_myCamp != 1) m_myCamp = 0;
    m_netSock = m_launch.netSocket;
    m_netClosing = false;

    // ---------- 窗口标题：标明模式/难度/阵营 ----------
    CString title;
    if (m_aiMode) {
        title.Format(L"斗兽棋 · 人机对战（%s难度）", DifficultyName());
    }
    else {
        title.Format(L"斗兽棋 · 联机对战（%s%s）",
            m_myCamp == 0 ? L"红方" : L"蓝方",
            m_launch.netIsHost ? L"·主机" : L"·客机");
    }
    SetWindowText(title);

    // ---------- 顶部按钮：重新开局 / 返回大厅 ----------
    {
        CRect rc;
        GetClientRect(&rc);
        const int btnW = 96, btnH = 24, gap = 8;
        int y = 5;
        int x2 = rc.right - 24 - btnW;
        int x1 = x2 - gap - btnW;

        m_uiFont.CreatePointFont(105, L"微软雅黑");
        m_btnRestart.Create(m_aiMode ? L"重新开局" : L"重新开局", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            CRect(x1, y, x1 + btnW, y + btnH), this, IDC_GAME_RESTART);
        m_btnBack.Create(L"返回大厅", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            CRect(x2, y, x2 + btnW, y + btnH), this, IDC_GAME_BACK);
        m_btnRestart.SetFont(&m_uiFont);
        m_btnBack.SetFont(&m_uiFont);

        if (!m_aiMode) m_btnRestart.EnableWindow(FALSE);   // 联机对局不支持单方重开
    }

    // ---------- 引擎复位并开局 ----------
    Engine_SetAiLevel(m_aiLevel);
    StartNewGame();

    // ---------- 联机模式：启动接收线程 ----------
    if (!m_aiMode && m_netSock != INVALID_SOCKET) {
        m_netRecvThread = Net_StartGameRecv(m_hWnd, m_netSock);
    }

    return TRUE;
}

void CanimalChessDlg::StartNewGame()
{
    KillTimer(GAME_TIMER_AI);
    m_selectedIdx = -1;
    Engine_Startup();          // 内部会自动中止并回收上一局的 AI 后台思考
    Invalidate(FALSE);
}

void CanimalChessDlg::OnDestroy()
{
    KillTimer(GAME_TIMER_AI);
    if (!m_aiMode && m_netSock != INVALID_SOCKET) {
        Net_CloseSession(m_netSock, m_netRecvThread);   // 断开并回收接收线程
    }
    CDialogEx::OnDestroy();
}

void CanimalChessDlg::DrawMoveArrow(CDC* pDC, CPoint ptStart, CPoint ptEnd)
{
    if (ptStart == ptEnd) return;

    CPen pen(PS_SOLID, 3, RGB(255, 69, 0));
    CPen* pOldPen = pDC->SelectObject(&pen);
    CBrush brush(RGB(255, 69, 0));
    CBrush* pOldBrush = pDC->SelectObject(&brush);

    pDC->Ellipse(ptStart.x - 5, ptStart.y - 5, ptStart.x + 5, ptStart.y + 5);

    pDC->MoveTo(ptStart);
    pDC->LineTo(ptEnd);

    double angle = atan2((double)(ptEnd.y - ptStart.y), (double)(ptEnd.x - ptStart.x));
    double arrowLen = 12.0;
    double arrowAngle = 0.5;

    CPoint p1(ptEnd.x - (int)(arrowLen * cos(angle - arrowAngle)),
        ptEnd.y - (int)(arrowLen * sin(angle - arrowAngle)));
    CPoint p2(ptEnd.x - (int)(arrowLen * cos(angle + arrowAngle)),
        ptEnd.y - (int)(arrowLen * sin(angle + arrowAngle)));

    CPoint pts[3] = { ptEnd, p1, p2 };
    pDC->Polygon(pts, 3);

    pDC->SelectObject(pOldPen);
    pDC->SelectObject(pOldBrush);
}

void CanimalChessDlg::OnPaint()
{
    CPaintDC dc(this);

    CRect clientRc;
    GetClientRect(&clientRc);
    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&dc, clientRc.Width(), clientRc.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    memDC.FillSolidRect(&clientRc, RGB(242, 244, 248));

    //拉取快照，纯依据快照数据绘制。
    MsgBoardSnapshot snap;
    Engine_GetSnapshot(snap);

    for (uint8_t i = 0; i < BOARD_CELL_COUNT; i++) {
        int row = i / BOARD_COLS;
        int col = i % BOARD_COLS;

        int px = OFFSET_X + col * GRID_SIZE;
        int py = OFFSET_Y + row * GRID_SIZE;
        CRect rc(px, py, px + GRID_SIZE, py + GRID_SIZE);

        TerrainType t = Engine_GetTerrainByIndex(i);
        if (t == TERRAIN_RIVER) {
            memDC.FillSolidRect(&rc, RGB(140, 200, 245));
            memDC.SetTextColor(RGB(0, 100, 180));
            memDC.DrawText(_T("～水～"), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (t == TERRAIN_RED_DEN) {
            memDC.FillSolidRect(&rc, RGB(255, 190, 200));
            memDC.SetTextColor(RGB(180, 0, 0));
            memDC.DrawText(_T("红穴"), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (t == TERRAIN_BLACK_DEN) {
            memDC.FillSolidRect(&rc, RGB(190, 210, 245));
            memDC.SetTextColor(RGB(0, 0, 180));
            memDC.DrawText(_T("蓝穴"), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (t == TERRAIN_RED_TRAP || t == TERRAIN_BLACK_TRAP) {
            memDC.FillSolidRect(&rc, RGB(230, 230, 230));
            memDC.SetTextColor(RGB(120, 120, 120));
            memDC.DrawText(_T("陷阱"), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else {
            memDC.FillSolidRect(&rc, RGB(252, 246, 232));
        }

        memDC.DrawEdge(&rc, EDGE_ETCHED, BF_RECT);

        uint8_t pc = snap.board[i];
        if (pc > 0) {
            CRect pieceRc = rc;
            pieceRc.DeflateRect(6, 6);

            bool isRed = (pc <= 8);
            CBrush pBrush(isRed ? RGB(255, 240, 240) : RGB(240, 245, 255));
            CPen pPen(PS_SOLID, 2, isRed ? RGB(200, 30, 30) : RGB(30, 90, 200));
            CPen* pOld = memDC.SelectObject(&pPen);
            CBrush* pOldB = memDC.SelectObject(&pBrush);

            memDC.Ellipse(&pieceRc);

            memDC.SetBkMode(TRANSPARENT);
            memDC.SetTextColor(isRed ? RGB(180, 0, 0) : RGB(0, 50, 180));

            CFont font;
            font.CreatePointFont(130, _T("微软雅黑"));
            CFont* pOldFont = memDC.SelectObject(&font);

            CString strName = PieceNameW(pc);
            memDC.DrawText(strName, &pieceRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            memDC.SelectObject(pOldFont);
            memDC.SelectObject(pOld);
            memDC.SelectObject(pOldB);
        }
    }

    if (m_selectedIdx >= 0 && m_selectedIdx < BOARD_CELL_COUNT) {
        int row = m_selectedIdx / BOARD_COLS;
        int col = m_selectedIdx % BOARD_COLS;
        CRect selRc(OFFSET_X + col * GRID_SIZE, OFFSET_Y + row * GRID_SIZE,
            OFFSET_X + (col + 1) * GRID_SIZE, OFFSET_Y + (row + 1) * GRID_SIZE);

        CPen goldPen(PS_SOLID, 4, RGB(255, 190, 0));
        CPen* pOldPen = memDC.SelectObject(&goldPen);
        memDC.SelectStockObject(NULL_BRUSH);
        memDC.Rectangle(&selRc);
        memDC.SelectObject(pOldPen);
    }

    if (snap.lastSrc < BOARD_CELL_COUNT && snap.lastDst < BOARD_CELL_COUNT) {
        int sRow = snap.lastSrc / BOARD_COLS, sCol = snap.lastSrc % BOARD_COLS;
        int dRow = snap.lastDst / BOARD_COLS, dCol = snap.lastDst % BOARD_COLS;

        CPoint ptStart(OFFSET_X + sCol * GRID_SIZE + GRID_SIZE / 2, OFFSET_Y + sRow * GRID_SIZE + GRID_SIZE / 2);
        CPoint ptEnd(OFFSET_X + dCol * GRID_SIZE + GRID_SIZE / 2, OFFSET_Y + dRow * GRID_SIZE + GRID_SIZE / 2);

        DrawMoveArrow(&memDC, ptStart, ptEnd);
    }

    //状态文本提示（右侧预留按钮区域）。
    CRect statusRc(OFFSET_X, 8, clientRc.right - 230, 32);
    memDC.SetBkMode(TRANSPARENT);
    memDC.SetTextColor(RGB(60, 60, 60));
    CFont statusFont;
    statusFont.CreatePointFont(105, _T("微软雅黑"));
    CFont* pOldF = memDC.SelectObject(&statusFont);

    CString strInfo;
    if (snap.gameStatus == 1) strInfo = _T("【对局结束】红方取得胜利！");
    else if (snap.gameStatus == 2) strInfo = _T("【对局结束】蓝方取得胜利！");
    else if (snap.gameStatus == 3) strInfo = _T("【对局结束】双方和棋！");
    else {
        if (m_aiMode) {
            strInfo = (snap.currentTurn == 0)
                ? _T("轮到你（红方）走子")
                : _T("电脑（蓝方）思考中……");
        }
        else {
            strInfo = (snap.currentTurn == m_myCamp)
                ? _T("轮到你走子")
                : _T("等待对方走子……");
        }
    }

    CString strHead;
    if (m_aiMode) {
        strHead.Format(_T("[人机·%s] "), DifficultyName());
    }
    else {
        strHead.Format(_T("[联机·%s] "), m_myCamp == 0 ? _T("红方") : _T("蓝方"));
    }
    memDC.DrawText(strHead + strInfo, &statusRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    memDC.SelectObject(pOldF);

    dc.BitBlt(0, 0, clientRc.Width(), clientRc.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
}

HCURSOR CanimalChessDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CanimalChessDlg::OnLocalMoveDone(uint8_t srcIdx, uint8_t dstIdx)
{
    if (m_aiMode) {
        Invalidate(FALSE);
        UpdateWindow();
        // 玩家走完后在后台触发 AI（不阻塞界面）
        if (Engine_TriggerAiAsync()) {
            SetTimer(GAME_TIMER_AI, 150, nullptr);
        }
        else {
        }
        Invalidate(FALSE);
    }
    else {
        NetMoveSync mv;
        mv.srcIndex = srcIdx;
        mv.dstIndex = dstIdx;
        if (!Net_SendFrame(m_netSock, NET_MSG_MOVE_SYNC, &mv, (int)sizeof(mv))) {
            HandlePeerGone();
            return;
        }
        Invalidate(FALSE);
    }
}

void CanimalChessDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    {
        CRect wr;
        GetWindowRect(&wr);
    }

    if (point.x < OFFSET_X || point.y < OFFSET_Y) {
        CDialogEx::OnLButtonDown(nFlags, point);
        return;
    }

    if (point.x < OFFSET_X || point.y < OFFSET_Y) {
        CDialogEx::OnLButtonDown(nFlags, point);
        return;
    }

    int col = (point.x - OFFSET_X) / GRID_SIZE;
    int row = (point.y - OFFSET_Y) / GRID_SIZE;

    if (col >= 0 && col < BOARD_COLS && row >= 0 && row < BOARD_ROWS)
    {
        uint8_t clickedIdx = static_cast<uint8_t>(row * BOARD_COLS + col);

        MsgBoardSnapshot snap;
        Engine_GetSnapshot(snap);

        bool myTurn = IsMyTurn(snap);
        {
            CString dump;
            for (int rr = 0; rr < BOARD_ROWS; rr++) {
                CString line;
                for (int cc = 0; cc < BOARD_COLS; cc++) {
                    CString t;
                    t.Format(L"%2d ", snap.board[rr * BOARD_COLS + cc]);
                    line += t;
                }
                dump += line + L" | ";
            }
        }

        if (snap.gameStatus == 0) {
            if (!myTurn) {
                // 非本机回合：清除残留选中状态
                if (m_selectedIdx >= 0) { m_selectedIdx = -1; Invalidate(FALSE); }
            }
            else if (m_selectedIdx == -1) {
                // 未选中时，点击己方棋子则高亮
                if (IsMyColor(snap.board[clickedIdx])) {
                    m_selectedIdx = clickedIdx;
                    Invalidate(FALSE);
                }
            }
            else {
                if (IsMyColor(snap.board[clickedIdx])) {
                    // 改选其他己方棋子
                    m_selectedIdx = clickedIdx;
                    Invalidate(FALSE);
                }
                else if (clickedIdx != m_selectedIdx) {
                    // 提交走子请求
                    uint8_t src = static_cast<uint8_t>(m_selectedIdx);
                    bool moved = Engine_TryMove(src, clickedIdx);
                    m_selectedIdx = -1;
                    if (moved) {
                        OnLocalMoveDone(src, clickedIdx);
                    }
                    else {
                        Invalidate(FALSE);   // 非法走子被忽略
                    }
                    else if (m_gameMode == GameMode::LanHost) {
                        // 房主先在权威棋盘执行红方走子，再把已确认走子广播给客机。
                        if (Engine_TryMove(src, clickedIdx)) {
                            if (!m_lanSession.SendMove(src, clickedIdx)) {
                                SetNetworkStatus(_T("状态：红方走子已执行，但发送失败"));
                            }
                            Invalidate(FALSE);
                        }
                    }
                    else {
                        // 客机只做无副作用预检；必须等房主回传确认后才修改棋盘。
                        if (Engine_IsLegalMove(src, clickedIdx)) {
                            if (m_lanSession.SendMove(src, clickedIdx)) {
                                m_waitingForGuestConfirmation = true;
                                m_pendingSrc = src;
                                m_pendingDst = clickedIdx;
                                SetNetworkStatus(_T("状态：走子已发送，等待房主确认..."));
                            }
                            else {
                                SetNetworkStatus(_T("状态：走子发送失败"));
                            }
                            Invalidate(FALSE);
                        }
                    }
                    // 非法走子同样要清除刚刚取消的选中框。
                    Invalidate(FALSE);
                }
            }
        }
    }

    CDialogEx::OnLButtonDown(nFlags, point);
}

void CanimalChessDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == GAME_TIMER_AI) {
        if (!Engine_IsAiThinking()) {
            KillTimer(GAME_TIMER_AI);
            // 电脑落子已完成：立即同步重绘，避免画面停留在思考前
            Invalidate(FALSE);
            UpdateWindow();
            return;
        }
        Invalidate(FALSE);   // 思考中：刷新“思考中……”状态
    }
    CDialogEx::OnTimer(nIDEvent);
}

void CanimalChessDlg::OnBnClickedRestart()
{
    if (m_aiMode) StartNewGame();
}

void CanimalChessDlg::OnBnClickedBackLobby()
{
    EndDialog(IDOK);   // App 收到 IDOK 后回到大厅
}

LRESULT CanimalChessDlg::OnNetMove(WPARAM wParam, LPARAM /*lParam*/)
{
    if (m_netClosing || m_aiMode) return 0;
    uint8_t src = static_cast<uint8_t>((wParam >> 8) & 0xFF);
    uint8_t dst = static_cast<uint8_t>(wParam & 0xFF);

    // 对端走子：以同样的确定性规则本地校验并落子
    if (Engine_TryMove(src, dst)) {
        m_selectedIdx = -1;
        Invalidate(FALSE);
    }
    return 0;
}

LRESULT CanimalChessDlg::OnNetClosed(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    HandlePeerGone();
    return 0;
}

void CanimalChessDlg::HandlePeerGone()
{
    if (m_netClosing) return;
    m_netClosing = true;

    KillTimer(GAME_TIMER_AI);
    Net_CloseSession(m_netSock, m_netRecvThread);   // 回收连接与接收线程

    if (GetSafeHwnd()) {
        MessageBox(_T("对方已断开连接，本局结束。"), _T("联机对战"), MB_OK | MB_ICONINFORMATION);
    }
    EndDialog(IDOK);                 // 返回大厅
}
