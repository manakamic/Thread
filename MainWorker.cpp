//
// C++20 時代のスレッド
// スタンダード処理
// std::async + std::future を使ったスレッドの完了待ちを行うサンプル
// メリット : 一般的なスレッド処理
// デメリット : スレッドキャンセルを行いたい場合は C++ のスレッド構文をより理解する必要がある
//
#include <DxLib.h>
#include "Mandelbrot.h"

namespace {
    constexpr auto WINDOW_TITLE = "Thread Sample";
    constexpr auto SCREEN_WIDTH = 1280;
    constexpr auto SCREEN_HEIGHT = 720;
    constexpr auto SCREEN_DEPTH = 32;

    constexpr auto PIXEL_WIDTH = 1024;
    constexpr auto PIXEL_HEIGHT = 1024;
}

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

    auto startThreads = false;
    auto allThreadFinished = false;

    // 本 cpp のメイン インスタンス
    std::unique_ptr<Mandelbrot> mandelbrotWorker = std::make_unique<Mandelbrot>(threadNum, PIXEL_WIDTH, PIXEL_HEIGHT);

    if (!mandelbrotWorker->Initialize()) {
        DxLib_End();
        return -1;
    }

    SetDrawScreen(DX_SCREEN_BACK);

    // 描画関連の変数
    auto threadFrameCount = 0;
    auto strColor = GetColor(255, 255, 255);
    auto cgHandle = -1;

    while (ProcessMessage() != -1) {
        if (1 == CheckHitKey(KEY_INPUT_ESCAPE)) {
            break;
        }

        // 全スレッドの処理完了をチェックする
        if (!allThreadFinished && startThreads) {
            allThreadFinished = mandelbrotWorker->CheckThreadFinished();

            if (allThreadFinished) {
                if (mandelbrotWorker->CreateSoftImageFromGraph()) {
                    cgHandle = mandelbrotWorker->GetCgHandle();
                }
            }
        }

        // スペースキーでスレッド作成(他のフラグと組み合わせているのでトリガー処理は不要)
        if (threadNum != 0 && !startThreads && (1 == CheckHitKey(KEY_INPUT_SPACE))) {
            startThreads = mandelbrotWorker->StartThread();
        }

        // スレッド処理が完了している場合は、再度スレッドを開始出来る様に再初期化する
        if (startThreads && allThreadFinished && (1 == CheckHitKey(KEY_INPUT_RETURN))) {
            startThreads = false;
            allThreadFinished = false;
            threadFrameCount = 0;
            mandelbrotWorker->DeleteGraphHandle();
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

    mandelbrotWorker.reset();

    DxLib_End();

    return 0;
}
