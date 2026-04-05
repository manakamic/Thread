#include <DxLib.h>
#include <complex>
#include <chrono>
#include <random>
#include "Mandelbrot.h"

// 秒を s で表記するための using 宣言
using namespace std::literals::chrono_literals;

namespace {
    // マンデルブロ集合の計算設定
    constexpr auto MAX_ITERATIONS = 512;
    constexpr auto X_MIN = -2.0;
    constexpr auto X_MAX = 1.0;
    constexpr auto Y_MIN = -1.5;
    constexpr auto Y_MAX = 1.5;

    // 
    std::vector<unsigned int> pixelArray;

    // マンデルブロ集合の色付け用のランダム
    std::random_device seed_gen;
    std::mt19937_64 random(seed_gen());

    int getRandom(const int min, const int max) {
        std::uniform_int_distribution<int>  distr(min, max);

        return distr(random);
    }
}

Mandelbrot::~Mandelbrot() {
    // 停止要求を送信
    for (auto& thread : workerThreads) {
        thread.request_stop();
    }

    // 各スレッドのコンディション変数を通知して待機状態を解除する
    for (auto& control : controls) {
        std::lock_guard lock(control.mutex);
        control.conditionVariable.notify_one();
    }

    // 明示的に各スレッドの終了を待機
    for (auto& thread : workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    controls.clear();
    workerThreads.clear();
    futures.clear();

    if (-1 != softHandle) {
        DeleteSoftImage(softHandle);
        softHandle = -1;
    }

    if (-1 != cgHandle) {
        DeleteGraph(cgHandle);
        cgHandle = -1;
    }
}

bool Mandelbrot::Initialize() {
    if (0 == threadNum || 0 == pixelWidth || 0 == pixelHeight) {
        return false;
    }

    pixelArray.assign(pixelWidth * pixelHeight, 0xFF000000); // ARGB形式で初期化

    // ワーカースレッドを起動
    for (auto i = 0; i < threadNum; ++i) {
        workerThreads.emplace_back([this, i](std::stop_token stop_token) {
            this->MandelbrotWorkerThreadTask(stop_token, std::ref(controls[i]));
        });
    }

    softHandle = MakeXRGB8ColorSoftImage(pixelWidth, pixelHeight);

    return -1 != softHandle;
}

bool Mandelbrot::StartThread() {
    // 前回の結果をクリア
    futures.clear();

    // 各スレッドに割り当てる行数を計算
    auto rowsPerThread = pixelHeight / threadNum;

    auto rParam = getRandom(3, 12);
    auto gParam = getRandom(3, 12);
    auto bParam = getRandom(3, 12);

    for (auto i = 0; i < threadNum; ++i) {
        auto startY = i * rowsPerThread;
        auto endY = (i == threadNum - 1) ? pixelHeight : (i + 1) * rowsPerThread;
        Task task{ startY, endY, pixelWidth, pixelHeight, rParam, gParam, bParam };

        futures.push_back(task.result.get_future());

        auto& control = controls[i];

        control.task = std::move(task);
        std::lock_guard lock(control.mutex);
        control.conditionVariable.notify_one();
    }

    return true;
}

bool Mandelbrot::CheckThreadFinished() {
    if (futures.empty()) {
        return false;
    }

    auto count = 0;

    for (const auto& future : futures) {
        // 0 秒のウエイトでスレッドの状態を取得
        //auto status = future.wait_for(std::chrono::seconds(0));
        auto status = future.wait_for(0s);

        if (status == std::future_status::ready) {
            ++count;
        }
    }

    return count == threadNum;
}

bool Mandelbrot::CreateSoftImageFromGraph() {
    if (-1 == softHandle) {
        return false;
    }

    for (auto y = 0; y < pixelHeight; ++y) {
        for (auto x = 0; x < pixelWidth; ++x) {
            const auto& rgb = pixelArray[y * pixelWidth + x];
            const auto r = 0x000000FF & (rgb >> 16);
            const auto g = 0x000000FF & (rgb >> 8);
            const auto b = 0x000000FF &  rgb;

            DrawPixelSoftImage(softHandle, x, y, r, g, b, 255);
        }
    }

    cgHandle = CreateGraphFromSoftImage(softHandle);

    return -1 != cgHandle;
}

void Mandelbrot::DeleteGraphHandle() {
    if (-1 != cgHandle) {
        DeleteGraph(cgHandle);
        cgHandle = -1;
    }
}

void Mandelbrot::MandelbrotWorkerThreadTask(std::stop_token stopToken, Mandelbrot::TaskControl& taskControl) {
    while (!stopToken.stop_requested()) {
        std::unique_lock lock(taskControl.mutex);

        // タスクが来るまで待機（スリープ）
        // stop_requested()も一緒にチェックすることで、jthreadからの停止要求にも速やかに応答できる
        taskControl.conditionVariable.wait(lock, [&] {
            return taskControl.task.has_value() || stopToken.stop_requested();
        });

        lock.unlock(); // ロックを解除する

        // jthreadからの停止要求で起きた場合はループを抜ける
        if (stopToken.stop_requested()) {
            return;
        }

        // タスクを取り出して処理を開始
        if (taskControl.task.has_value()) {
            Task task = std::move(taskControl.task.value());

            taskControl.task.reset(); // タスクを取り出したので空にする

            for (auto y = task.startY; y < task.endY; ++y) {
                for (auto x = 0; x < task.pixelWidth; ++x) {
                    if (stopToken.stop_requested()) {
                        break;
                    }

                    pixelArray[y * task.pixelWidth + x] = CalculateMandelbrotColor(x,
                                                                                   y,
                                                                                   task.pixelWidth,
                                                                                   task.pixelHeight,
                                                                                   task.rParam,
                                                                                   task.gParam,
                                                                                   task.bParam,
                                                                                   stopToken);
                }

                if (stopToken.stop_requested()) {
                    break;
                }
            }

            if (!stopToken.stop_requested()) {
                // promiseを通じてメインスレッドに結果を返す(ダミー)
                task.result.set_value(true);
            }
        }
    }
}

unsigned int Mandelbrot::CalculateMandelbrotColor(const int px, const int py,
                                                  const int pixelWidth, const int pixelHeight,
                                                  const int rParam, const int gParam, const int bParam,
                                                  const std::stop_token& stopToken) {
    const auto x = X_MIN + (static_cast<double>(px) / pixelWidth)  * (X_MAX - X_MIN);
    const auto y = Y_MIN + (static_cast<double>(py) / pixelHeight) * (Y_MAX - Y_MIN);

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
        auto r = (iterations * rParam) % 255;
        auto g = (iterations * gParam) % 255;
        auto b = (iterations * bParam) % 255;

        rgb = 0xFF000000 | (r << 16) | (g << 8) | b; // ARGB
    }

    return rgb;
}
