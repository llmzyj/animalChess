// animalChess.cpp : 定义应用程序的类行为。
//
// 流程：大厅(模式/难度/联机连接) -> 对局窗口 -> 返回大厅 -> ……
#include "pch.h"
#include "framework.h"
#include "animalChess.h"
#include "animalChessDlg.h"
#include "LobbyDlg.h"
#include "AnimalChessNet.h"
#include "engine.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CanimalChessApp, CWinApp)
    ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

CanimalChessApp::CanimalChessApp()
{
}

CanimalChessApp theApp;

BOOL CanimalChessApp::InitInstance()
{
    CWinApp::InitInstance();

    Net_Startup();   // 联机模块初始化（人机模式不连接也安全）

    GameLaunchInfo launch;
    launch.mode = GAME_MODE_AI;
    launch.aiLevel = 2;
    launch.netIsHost = true;
    launch.netLocalCamp = 0;
    launch.netSocket = INVALID_SOCKET;

    for (;;) {
        // 1. 大厅：选择 人机/联机、难度、联机连接
        CLobbyDlg lobby(&launch);
        if (lobby.DoModal() != IDOK) {
            break;                       // 用户退出程序
        }

        // 2. 对局窗口
        CanimalChessDlg dlg;
        dlg.SetLaunchInfo(launch);
        INT_PTR gameRet = dlg.DoModal();
        // 对局结束后套接字已由对局窗口回收
        launch.netSocket = INVALID_SOCKET;

        if (gameRet != IDOK) {
            break;                       // 直接关闭窗口 -> 退出程序
        }
        // gameRet == IDOK：点击 [返回大厅]，回到第 1 步
    }

    Engine_Shutdown();   // 回收可能仍在后台的 AI 思考线程
    Net_Cleanup();
    return FALSE;
}
