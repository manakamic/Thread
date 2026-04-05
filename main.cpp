//
// C++20 時代のスレッド
// スタンダード処理
// std::async + std::future を使ったスレッドの完了待ちを行うサンプル
// メリット : 一般的なスレッド処理
// デメリット : スレッドキャンセルを行いたい場合は C++ のスレッド構文をより理解する必要がある
//
#include <DxLib.h>
#include <thread>
#include <future>
#include <complex>
#include <chrono>
#include <vector>

// 秒を s で表記するための using 宣言
using namespace std::literals::chrono_literals;

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

// 前方宣言
unsigned int CalculateMandelbrotColor(const int px, const int py);
void MandelbrotThreadTask(const int startY, const int endY, std::vector<unsigned int>& pixelArray);
bool CreateThread(const int threadNum, std::vector<std::future<void>>& futures, std::vector<unsigned int>& pixelArray);
bool CheckThreadFinished(const std::vector<std::future<void>>& futures);
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

    // 本 cpp のメイン変数
    std::vector<std::future<void>> futures; // std::async + std::future を使用する方法

    auto startThreads = false;
    auto allThreadFinised = false;

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
        if (!allThreadFinised && startThreads) {
            allThreadFinised = CheckThreadFinished(futures);

            if (allThreadFinised) {
                cgHandle = CreateSoftImageFromGraph(softHandle, pixelArray);
            }
        }

        // スペースキーでスレッド作成(他のフラグと組み合わせているのでトリガー処理は不要)
        if (threadNum != 0 && !startThreads && (1 == CheckHitKey(KEY_INPUT_SPACE))) {
            startThreads = CreateThread(threadNum, futures, pixelArray);
        }

        if (!allThreadFinised && startThreads) {
            threadFrameCount++; // スレッド処理中のフレーム数をカウント
        }

        ClearDrawScreen();

        if ((-1 != cgHandle) && allThreadFinised) {
            DrawGraph(0, 0, cgHandle, FALSE);
        }

        DrawFormatString(10, 10, strColor, _T("Thread Num(%d) : Thread Frame(%d)"), threadNum, threadFrameCount);

        ScreenFlip();
    }

    // もしスレッドが終了していない場合は、ここで待ちが発生する
    for (auto&& future : futures) {
        if (future.valid()) { // future が有効な場合のみ wait を呼び出す
            future.wait();
        }
    }

    futures.clear();

    if (-1 != cgHandle) {
        DeleteGraph(cgHandle);
        cgHandle = -1;
    }

    DxLib_End();

    return 0;
}

// スレッドで行う関数
// std::async は、非同期にタスクを実行してその結果を std::future で返す
// という処理を手軽に行うための高レベルなユーティリティ
// しかし、インターフェースにはキャンセルのための std::stop_token を直接渡す仕組みが存在せず
// キャンセルを行いたい場合は、もう少し複雑な実装を行う必要がある
void MandelbrotThreadTask(const int startY, const int endY, std::vector<unsigned int>& pixelArray) {
    for (auto y = startY; y < endY; ++y) {
        for (auto x = 0; x < PIXEL_WIDTH; ++x) {
            pixelArray[y * PIXEL_WIDTH + x] = CalculateMandelbrotColor(x, y);
        }
    }
}

// マンデルブロ集合の色を計算する関数
// スレッドに渡した関数から呼ばれる関数は、
// 呼び出し元と同じコンテキスト(スレッド)の処理となるためグローバル変数等の使用には注意が必要
// 本関数は値を計算して返すだけなので、スレッドセーフである
// なお本関数がマンデルブロ計算におてい重たい処理になる
unsigned int CalculateMandelbrotColor(const int px, const int py) {
    const auto x = X_MIN + (static_cast<double>(px) / PIXEL_WIDTH)  * (X_MAX - X_MIN);
    const auto y = Y_MIN + (static_cast<double>(py) / PIXEL_HEIGHT) * (Y_MAX - Y_MIN);

    auto iterations = 0;
    std::complex<double> c{ x, y };
    std::complex<double> z{ 0.0, 0.0 };

    while (std::norm(z) < 4.0 && iterations < MAX_ITERATIONS) {
        z = z * z + c;
        ++iterations;
    }

    unsigned int rgb = 0xFF000000;

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
bool CreateThread(const int threadNum, std::vector<std::future<void>>& futures, std::vector<unsigned int>& pixelArray) {
    // 各スレッドに割り当てる行数を計算
    auto rowsPerThread = PIXEL_HEIGHT / threadNum;

    for (auto i = 0; i < threadNum; ++i) {
        auto startY = i * rowsPerThread;
        auto endY = (i == threadNum - 1) ? PIXEL_HEIGHT : (i + 1) * rowsPerThread;

        // 別スレッドに参照で変数を渡してもコピーにしかならないので(規格上のデフォルト挙動)
        // std::ref で渡す(明示的な参照渡し 且つ ダングリング参照などへの責任を持つ)
        futures.push_back(std::async(std::launch::async, MandelbrotThreadTask, startY, endY, std::ref(pixelArray)));
    }

    return !futures.empty();
}

// 全てのスレッドの処理完了をチェックする
bool CheckThreadFinished(const std::vector<std::future<void>>& futures) {
    if (futures.empty()) {
        return false;
    }

    for (const auto& future : futures) {
        // 0 秒のウエイトでスレッドの状態を取得
        //auto status = future.wait_for(std::chrono::seconds(0));
        auto status = future.wait_for(0s);

        if (status != std::future_status::ready) {
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
