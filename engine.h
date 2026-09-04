#pragma once
#include <cstdint>
#include <atomic>
#include <vector>
#include <array>

constexpr int BOARD_ROWS = 7;
constexpr int BOARD_COLS = 9;
constexpr int BOARD_CELL_COUNT = 63;

constexpr int BOARD_RANK_TOP = 3;
constexpr int BOARD_RANK_BOTTOM = 9;
constexpr int BOARD_FILE_LEFT = 3;
constexpr int BOARD_FILE_RIGHT = 11;

enum TerrainType {
    TERRAIN_LAND = 0,
    TERRAIN_RED_DEN = 1,
    TERRAIN_RED_TRAP = 2,
    TERRAIN_RIVER = 3,
    TERRAIN_BLACK_TRAP = 4,
    TERRAIN_BLACK_DEN = 5
};

#pragma pack(push, 1)
struct MsgBoardSnapshot {
    uint8_t gameStatus;
    uint8_t currentTurn;
    uint8_t lastSrc;
    uint8_t lastDst;
    uint8_t board[BOARD_CELL_COUNT];
};
#pragma pack(pop)

struct MoveRecord {
    int moveVal;
    int capturedPiece;
    int movedPiece;
};

class JungleBoard {
public:
    JungleBoard();
    void Reset();

    bool MakeMove(int mv);
    void UndoMove();

    int  GenerateMoves(std::vector<int>& outMoves, bool capturesOnly = false) const;
    bool IsLegalMove(int mv) const;
    bool IsGameOver() const;
    bool IsMate() const;
    bool IsRepetition() const;

    int  GetPiece(int sq) const;
    int  GetSide() const { return m_side; }

    bool CanMove(int src, int dst) const;
    bool CanJump(int src, int dst) const;
    bool CanEat(int src, int dst) const;

    static bool InBoard(int sq);
    static TerrainType GetTerrain(int sq);
    static int CoordToSq(int x, int y) { return x + (y << 4); }
    static int SqToFileX(int sq) { return sq & 15; }
    static int SqToRankY(int sq) { return sq >> 4; }

    static int IndexToSq(uint8_t idx);
    static uint8_t SqToIndex(int sq);

private:
    std::array<int, 256> m_squares;
    int m_side;
    std::vector<MoveRecord> m_history;

    void AddPiece(int sq, int piece);
    void DelPiece(int sq, int piece);
};

class JungleEvaluator {
public:
    static int Evaluate(const JungleBoard& board);
};

class AlphaBetaSearcher {
public:
    AlphaBetaSearcher();
    int SearchBestMove(JungleBoard& board, int depth);

    // 思考控制钩子：
    //   pAbort    置位后搜索会在数千节点内快速放弃（返回值不可用，调用方丢弃）；
    //   nodeLimit 本次搜索的节点预算（0 = 不限），超预算同样快速放弃并置 LimitHit，
    //             用于“困难档”迭代加深控制单层耗时。
    // 传 nullptr / 0 表示不启用。
    void SetAbortFlag(std::atomic<bool>* pAbort, int nodeLimit = 0);
    bool LimitHit() const { return m_limitHit; }

private:
    std::atomic<bool>* m_pAbort;
    int  m_nodeCount;
    int  m_nodeLimit;
    bool m_limitHit;
    std::array<int, 65536> m_historyTable;
    int QuiescenceSearch(JungleBoard& board, int alpha, int beta, int depth);
    int AlphaBeta(JungleBoard& board, int depth, int alpha, int beta, bool isRoot = false);
};

//对外导出的通用数据与操作接口，方便后续直接对接UE。
void Engine_Startup();
void Engine_GetSnapshot(MsgBoardSnapshot& outSnapshot);
bool Engine_IsLegalMove(uint8_t srcIdx, uint8_t dstIdx);
bool Engine_TryMove(uint8_t srcIdx, uint8_t dstIdx);
bool Engine_TriggerAi();
TerrainType Engine_GetTerrainByIndex(uint8_t idx);
const char* Engine_GetPieceName(uint8_t pc);

//==================== 大厅新增：AI 难度等级 ====================
// 难度档位：1 = 简单，2 = 中等，3 = 困难。
// 对应关系：简单≈浅层搜索+随机失误，中等≈标准 Alpha-Beta，困难≈更深层搜索。
// 建议在 Engine_Startup 之前调用，对本局生效。
void Engine_SetAiLevel(int level);
int  Engine_GetAiLevel();

//==================== 大厅新增：异步 AI（人机模式核心） ====================
// 与旧接口 Engine_TriggerAi(同步) 的区别：
//   - 本接口立即返回，搜索在独立后台线程进行，UI 不会被卡住；
//   - 搜索基于棋盘副本，完成后在同一把引擎锁内校验并落子；
//   - 通过 Engine_IsAiThinking 轮询是否思考完成。
// 调用前提：当前局面轮到蓝方(电脑) 且没有正在进行的思考。
bool Engine_TriggerAiAsync();

// 后台 AI 是否正在思考
bool Engine_IsAiThinking();

// 请求中止后台思考（Engine_Startup / Engine_Shutdown 内部亦会自动调用）
void Engine_AbortAiRequest();

// 程序退出前调用：中止并回收 AI 线程（幂等，可多次调用）
void Engine_Shutdown();
