#pragma once
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>

class Mandelbrot {
public:
    class Task {
    public:
        // デフォルトコンストラクタ
        Task() = delete; // 必須パラメータがあるため明示的に削除

        // パラメータ付きコンストラクタ
        Task(const int startY, const int endY,
            const int pixelWidth, const int pixelHeight,
            const int rParam, const int gParam, const int bParam) :
            startY(startY), endY(endY),
            pixelWidth(pixelWidth), pixelHeight(pixelHeight),
            rParam(rParam), gParam(gParam), bParam(bParam),
            result{} {
        }

        // ムーブコンストラクタ
        Task(Task&& other) noexcept :
            startY(other.startY),
            endY(other.endY),
            pixelWidth(other.pixelWidth),
            pixelHeight(other.pixelHeight),
            rParam(other.rParam),
            gParam(other.gParam),
            bParam(other.bParam),
            result(std::move(other.result)) {
        }

        // ムーブ代入演算子
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                startY = other.startY;
                endY = other.endY;
                pixelWidth = other.pixelWidth;
                pixelHeight = other.pixelHeight;
                rParam = other.rParam;
                gParam = other.gParam;
                bParam = other.bParam;
                result = std::move(other.result);
            }
            return *this;
        }

        // コピーコンストラクタとコピー代入演算子を削除
        // (std::promiseはコピー不可能なため)
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // デストラクタ
        ~Task() = default;

        int startY;
        int endY;
        int pixelWidth;
        int pixelHeight;
        int rParam;
        int gParam;
        int bParam;
        // タスクの結果を格納するためのpromiseオブジェクト(ダミー)
        std::promise<bool> result;
    };

    class TaskControl {
    public:
        TaskControl() = default;

        // ムーブ構築・代入は明示的に定義
        TaskControl(TaskControl&& other) noexcept
            : mutex{}, conditionVariable{}, task{ std::move(other.task) } {
        }

        TaskControl& operator=(TaskControl&& other) noexcept {
            if (this != &other) {
                task = std::move(other.task);
            }
            return *this;
        }

        // デストラクタ
        ~TaskControl() = default;

        // mutexとcondition_variableはコピー不可なのでコピー禁止
        TaskControl(const TaskControl&) = delete;
        TaskControl& operator=(const TaskControl&) = delete;

        std::mutex mutex;
        std::condition_variable conditionVariable;
        std::optional<Task> task; // メインスレッドから渡されるタスク
    };

    Mandelbrot(const int threadNum, const int pixelWidth, const int pixelHeight) :
        softHandle{ -1 }, cgHandle{ -1 },
        pixelWidth{ pixelWidth }, pixelHeight{ pixelHeight },
        threadNum{ threadNum },
        controls{ static_cast<unsigned int>(threadNum) },
        workerThreads{ static_cast<unsigned int>(threadNum) } {}
    virtual ~Mandelbrot();

    bool Initialize();
    bool StartThread();
    bool CheckThreadFinished();
    bool CreateSoftImageFromGraph();
    void DeleteGraphHandle();

    void MandelbrotWorkerThreadTask(std::stop_token stopToken, Mandelbrot::TaskControl& taskControl);
    static unsigned int CalculateMandelbrotColor(const int px, const int py,
                                                 const int pixelWidth, const int pixelHeight,
                                                 const int rParam, const int gParam, const int bParam,
                                                 const std::stop_token& stopToken);

    int GetCgHandle() { return cgHandle; }

private:
    int softHandle;
    int cgHandle;
    int pixelWidth;
    int pixelHeight;
    int threadNum;

    std::vector<TaskControl> controls;
    std::vector<std::future<bool>> futures;
    std::vector<std::jthread> workerThreads;
};
