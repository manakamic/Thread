//
// C++20 時代のスレッド
// シンプル処理 2
// std::jthread + std::atomic_ref<bool> を使ったスレッドの完了待ちを行うサンプル
// メリット : 一番スレッド処理をシンプルに処理出来る
//            + 無駄な std::atomic を宣言せずにすむ
// デメリット : 多機能(複雑)な C++ の機能を使いきれていない
//
#include <DxLib.h>
#include <thread>
#include <atomic>
#include <complex>
#include <vector>

namespace {
    constexpr auto WINDOW_TITLE = "Thread Sample";
    constexpr auto SCREEN_WIDTH = 1280;
    constexpr auto SCREEN_HEIGHT = 720;
    constexpr auto SCREEN_DEPTH = 32;

    // マンデルブロ集合の計算設定
    constexpr auto MAX_ITERATIONS = 512;
    constexpr auto X_MIN = -2.0;
    constexpr auto X_MAX = 1.0;
    constexpr auto Y_MIN = -1.5;
    constexpr auto Y_MAX = 1.5;

    constexpr auto PIXEL_WIDTH = 1024;
    constexpr auto PIXEL_HEIGHT = 1024;
    constexpr auto PIXEL_ARRAY_SIZE = PIXEL_WIDTH * PIXEL_HEIGHT;
}

// std::vector<bool> の特殊化
// C++ 標準で特殊化されており、メモリ効率のために各要素を 1 ビットにパックする
// このため、vector[i] という式は、bool& ではなく、プロキシオブジェクト(std::vector<bool>::reference)になる
// std::vector<bool>::reference は std::atomic_ref<bool> がコンストラクタで要求する bool& と違ってしまう為
// std::vector<bool> は std::atomic_ref では使用できず、代わりの 8 bit 変数を使用する(unsigned char 等でも OK)
using Bool = std::uint8_t;

// 前方宣言
unsigned int CalculateMandelbrotColor(const int px, const int py, const std::stop_token& stopToken);
void MandelbrotThreadTask(std::stop_token stopToken,
                          const int startY,
                          const int endY,
                          Bool& finished,
                          std::vector<unsigned int>& pixelArray);
bool CreateThread(const int threadNum,
    std::vector<std::jthread>& threads,
    std::vector<Bool>& threadFinished,
    std::vector<unsigned int>& pixelArray);
void StopTasks(std::vector<std::jthread>& threads);
bool CheckThreadFinished(const std::vector<Bool>& threadFinished);
int CreateSoftImageFromGraph(int& softHandle, const std::vector<unsigned int>& pixelArray);

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    auto window_mode = FALSE;

#ifdef _DEBUG
    window_mode = TRUE;
#endif

    SetMainWindowText(WINDOW_TITLE);

    ChangeWindowMode(window_mode);

    SetGraphMode(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH);

    if (DxLib_Init() == -1) {
        return -1;
    }

    // Thread 関連の変数
    // CPU の最適なスレッド数を算出
    const auto threadNum = static_cast<int>(std::thread::hardware_concurrency()); // C++11

    std::vector<std::jthread> threads; // C++20
    std::vector<Bool> threadFinished(threadNum, 0); // 完了フラグ(非アトミック)

    // 本 cpp のメイン変数
    auto startThreads = false;
    auto allThreadFinished = false;

    SetDrawScreen(DX_SCREEN_BACK);

    // 描画関連の変数
    auto threadFrameCount = 0;
    auto cgHandle = -1;
    auto strColor = GetColor(255, 255, 255);
    auto softHandle = MakeXRGB8ColorSoftImage(PIXEL_WIDTH, PIXEL_HEIGHT);
    std::vector<unsigned int> pixelArray(PIXEL_ARRAY_SIZE); // ピクセルデータを格納する配列

    while (ProcessMessage() != -1) {
        if (1 == CheckHitKey(KEY_INPUT_ESCAPE)) {
            break;
        }

        // 全スレッドの処理完了をチェックする
        if (!allThreadFinished && startThreads) {
            allThreadFinished = CheckThreadFinished(threadFinished);

            if (allThreadFinished) {
                cgHandle = CreateSoftImageFromGraph(softHandle, pixelArray);
            }
        }

        // スペースキーでスレッド作成(他のフラグと組み合わせているのでトリガー処理は不要)
        if (threadNum != 0 && !startThreads && (1 == CheckHitKey(KEY_INPUT_SPACE))) {
            startThreads = CreateThread(threadNum, threads, threadFinished, pixelArray);
        }

        if (!allThreadFinished && startThreads) {
            threadFrameCount++; // スレッド処理中のフレーム数をカウント
        }

        ClearDrawScreen();

        if ((-1 != cgHandle) && allThreadFinished) {
            DrawGraph(0, 0, cgHandle, FALSE);
        }

        DrawFormatString(10, 10, strColor, _T("Thread Num(%d) : Thread Frame(%d)"), threadNum, threadFrameCount);

        ScreenFlip();
    }

    // std::jthread のデストラクタで std::stop_token が使用される
    threads.clear();

    if (-1 != cgHandle) {
        DeleteGraph(cgHandle);
        cgHandle = -1;
    }

    DxLib_End();

    return 0;
}

// スレッドで行う関数
// C++20 : std::jthread では std::stop_token (協調的キャンセルのために専用の仕組み)が導入されており
// 自動で std::jthread に渡される
void MandelbrotThreadTask(std::stop_token stopToken, const int startY, const int endY, Bool& finished, std::vector<unsigned int>& pixelArray) {
    for (auto y = startY; y < endY; ++y) {
        for (auto x = 0; x < PIXEL_WIDTH; ++x) {
            if (stopToken.stop_requested()) {
                break;
            }

            pixelArray[y * PIXEL_WIDTH + x] = CalculateMandelbrotColor(x, y, stopToken);
        }

        if (stopToken.stop_requested()) {
            break;
        }
    }

    if (!stopToken.stop_requested()) {
        // 非アトミックなフラグから atomic_ref を作成
        std::atomic_ref<Bool> ref(finished);

        // atomic_ref を通じて、完了状態(1)をアトミックに書き込む
        // memory_order_release を使い、この書き込みより前の処理が完了していることを保証する
        ref.store(1, std::memory_order_release);
    }
}

// std::stop_token を使用してスレッドを終了させる
void StopTasks(std::vector<std::jthread>& threads) {
    for (auto&& thread : threads) {
        thread.request_stop();
    }
}

// マンデルブロ集合の色を計算する関数
// スレッドに渡した関数から呼ばれる関数は、
// 呼び出し元と同じコンテキスト(スレッド)の処理となるためグローバル変数等の使用には注意が必要
// 本関数は値を計算して返すだけなので、スレッドセーフである
// なお本関数がマンデルブロ計算におてい重たい処理になる
unsigned int CalculateMandelbrotColor(const int px, const int py, const std::stop_token& stopToken) {
    const auto x = X_MIN + (static_cast<double>(px) / PIXEL_WIDTH) * (X_MAX - X_MIN);
    const auto y = Y_MIN + (static_cast<double>(py) / PIXEL_HEIGHT) * (Y_MAX - Y_MIN);

    // 毎回停止確認行うとオーバーヘッドが大きいので、一定の回数ごとにチェックする
    const auto stopCheckNum = 32;
    auto iterations = 0;
    std::complex<double> c{ x, y };
    std::complex<double> z{ 0.0, 0.0 };
    unsigned int rgb = 0xFF000000;

    while (std::norm(z) < 4.0 && iterations < MAX_ITERATIONS) {
        // 定期的に停止要求をチェック
        if (((iterations % stopCheckNum) == 0) && stopToken.stop_requested()) {
            return rgb;
        }

        z = z * z + c;
        ++iterations;
    }

    if (iterations != MAX_ITERATIONS) {
        // 色をグラデーションで表現(適当)
        auto r = (iterations *  6) % 255;
        auto g = (iterations *  3) % 255;
        auto b = (iterations * 12) % 255;

        rgb = 0xFF000000 | (r << 16) | (g << 8) | b; // ARGB
    }

    return rgb;
}

// スレッドを作成する
bool CreateThread(const int threadNum,
                  std::vector<std::jthread>& threads,
                  std::vector<Bool>& threadFinished,
                  std::vector<unsigned int>& pixelArray) {
    // 各スレッドに割り当てる行数を計算
    auto rowsPerThread = PIXEL_HEIGHT / threadNum;

    for (auto i = 0; i < threadNum; ++i) {
        auto startY = i * rowsPerThread;
        auto endY = (i == threadNum - 1) ? PIXEL_HEIGHT : (i + 1) * rowsPerThread;

        // std::jthred を emplace_back で新規作成
        threads.emplace_back(MandelbrotThreadTask, startY, endY, std::ref(threadFinished[i]), std::ref(pixelArray));
    }

    return true;
}

// 全てのスレッドの処理完了をチェックする
bool CheckThreadFinished(const std::vector<Bool>& threadFinished) {
    for (const auto& finished : threadFinished) {
        // 安全にフラグを読み取るために、メインスレッド側でも atomic_ref を使う
        std::atomic_ref<const Bool> ref(finished);

        // memory_order_acquire を使い、スレッド側(release)の処理結果を正しく観測する
        if (0 == ref.load(std::memory_order_acquire)) {
            return false;
        }
    }

    return true;
}

// マンデルブロ集合のカラー情報の配列を SoftImage(CPUピクセル処理) にコピーして
// Draw 用の画像にロード(GPUピクセル処理)する
int CreateSoftImageFromGraph(int& softHandle, const std::vector<unsigned int>& pixelArray) {
    for (auto y = 0; y < PIXEL_HEIGHT; ++y) {
        for (auto x = 0; x < PIXEL_WIDTH; ++x) {
            const auto& rgb = pixelArray[y * PIXEL_WIDTH + x];
            const auto r = 0x000000FF & (rgb >> 16);
            const auto g = 0x000000FF & (rgb >> 8);
            const auto b = 0x000000FF &  rgb;

            DrawPixelSoftImage(softHandle, x, y, r, g, b, 255);
        }
    }

    auto cgHandle = CreateGraphFromSoftImage(softHandle);

    if (-1 != cgHandle) {
        DeleteSoftImage(softHandle);
        softHandle = -1;
    }

    return cgHandle;
}
