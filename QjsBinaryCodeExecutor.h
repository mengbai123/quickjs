#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

// 前向声明，避免包含 quickjs 头文件，减少依赖
struct JSRuntime;
struct JSContext;

/**
 * @brief 执行模式枚举
 */
enum class ExecutionMode {
    BINARY, // 二进制字节码模式
    JS      // JavaScript 源代码模式
};

/**
 * @brief 简易版 QuickJS 二进制代码执行器
 *
 * 这个类封装了 qjs_bc.c 的核心功能，提供最简化的接口。
 * 支持加载 QuickJS 编译后的二进制字节码并执行，同时支持 Worker 线程。
 */
class QjsBinaryCodeExecutor {
public:
    // 构造函数和析构函数
    QjsBinaryCodeExecutor();

    ~QjsBinaryCodeExecutor();

    /**
     * @brief 设置调试模式
     * @param enabled true=启用调试输出，false=禁用
     */
    void setDebugMode(bool enabled) { debugEnabled_ = enabled; }

    /**
     * @brief 获取当前调试模式状态
     * @return true=已启用，false=已禁用
     */
    bool isDebugEnabled() const { return debugEnabled_; }

    /**
     * @brief 设置入口执行文件
     * @param entryFile 入口文件路径，如 "main.js" 或 "main.bc"
     */
    void setEntryFile(const std::string &entryFile) { entryFile_ = entryFile; }

    /**
     * @brief 获取当前入口执行文件
     * @return 入口文件路径
     */
    const std::string &getEntryFile() const { return entryFile_; }

    /**
     * @brief 设置执行模式
     * @param mode 执行模式（BINARY=二进制字节码，SOURCE=JS源代码）
     */
    void setExecutionMode(ExecutionMode mode) { executionMode_ = mode; }

    /**
     * @brief 获取当前执行模式
     * @return 执行模式
     */
    ExecutionMode getExecutionMode() const { return executionMode_; }

    // 禁止拷贝，允许移动（简化资源管理）
    QjsBinaryCodeExecutor(const QjsBinaryCodeExecutor &) = delete;

    QjsBinaryCodeExecutor &operator=(const QjsBinaryCodeExecutor &) = delete;

    QjsBinaryCodeExecutor(QjsBinaryCodeExecutor &&) noexcept;

    QjsBinaryCodeExecutor &operator=(QjsBinaryCodeExecutor &&) noexcept;

    /**
     * @brief 从文件加载二进制模块
     * @param filename 二进制文件路径
     *
     * 文件格式：每模块由三部分组成
     * - 1字节：load_only 标志（0=入口模块，1=预加载模块）
     * - 8字节：数据长度（uint64_t）
     * - N字节：实际的字节码数据
     */
    void loadModulesFromFile(const std::string &filename);

    /**
     * @brief 执行已加载的模块
     * @return 0=成功，其他=失败
     *
     * 执行流程：
     * 1. 创建 JSRuntime 运行时环境
     * 2. 创建 JSContext 上下文
     * 3. 预加载所有 load_only=1 的模块（供 Worker 使用）
     * 4. 执行第一个 load_only=0 的入口模块
     * 5. 进入事件循环，等待异步操作完成
     */
    int execute();

    /**
     * @brief 设置错误回调函数
     * @param callback 接收错误信息的回调
     *
     * 示例：
     * executer.onError([](const std::string& err) {
     *     std::cerr << "执行错误: " << err << std::endl;
     * });
     */
    void onError(std::function<void(JSRuntime *, JSContext *, const std::string &)> callback) {
        errorCallback_ = std::move(callback);
    }

    void onJsError(std::function<void(JSRuntime *, JSContext *, const std::string &, const std::string &, const std::string &)> callback) {
        jsErrorCallback_ = std::move(callback);
    }

    /**
     * @brief 设置执行后回调函数
     * @param callback 在代码执行完成、事件循环结束后调用的回调
     *
     * 该回调会在 execute() 方法中事件循环完成后触发，
     * 允许用户在执行完成后进行清理、日志记录等操作。
     *
     * 示例：
     * executer.afterExecute([](int exitCode) {
     *     std::cout << "执行完成，退出码: " << exitCode << std::endl;
     * });
     */
    void afterExecute(std::function<void(JSRuntime *, JSContext *)> callback) {
        afterExecuteCallback_ = std::move(callback);
    }

    /**
     * @brief 设置资源释放前回调函数
     * @param callback 在析构函数释放资源前调用的回调
     *
     * 该回调会在析构函数中 JSContext 和 JSRuntime 释放之前触发，
     * 允许用户在资源释放前进行最后的清理、状态保存等操作。
     *
     * 示例：
     * executer.beforeRelease([]() {
     *     std::cout << "即将释放 QuickJS 资源" << std::endl;
     * });
     */
    void beforeRelease(std::function<void(JSRuntime *, JSContext *)> callback) {
        beforeReleaseCallback_ = std::move(callback);
    }

    /**
     * @brief 设置上下文创建后回调函数
     * @param callback 在 JSContext 创建并预加载模块后调用的回调
     *
     * 该回调会在 createCustomContext() 方法中 JSContext 创建完成且预加载模块后触发，
     * 允许用户在上下文创建后进行自定义初始化、注册全局对象等操作。
     *
     * 示例：
     * executer.afterContextCreate([](JSContext* ctx) {
     *     std::cout << "上下文已创建，正在注册自定义函数..." << std::endl;
     *     // 注册自定义 C 函数到 JS 上下文
     * });
     */
    void afterContextCreate(std::function<void(JSRuntime *, JSContext *)> callback) {
        afterContextCreateCallback_ = std::move(callback);
    }

    void afterRuntimeCreate(std::function<void(JSRuntime *)> callback) {
        afterRuntimeCreateCallback_ = std::move(callback);
    }

    // 获取内部 JS 运行时和上下文（用于高级操作）
    JSRuntime *runtime() const noexcept { return runtime_; }
    JSContext *context() const noexcept { return context_; }

    // 公开的静态回调函数，供 QuickJS 的 C API 调用
    static JSContext *workerContextCallback(JSRuntime *rt, void *userdata);

private:
    // 模块数据结构
    struct Module {
        bool load_only; // 是否为预加载模块
        std::vector<uint8_t> data; // 字节码数据
    };

    std::vector<Module> modules_; // 存储所有加载的模块
    JSRuntime *runtime_ = nullptr; // JS 运行时实例
    JSContext *context_ = nullptr; // JS 上下文实例
    std::function<void(JSRuntime *, JSContext *, const std::string &)> errorCallback_; // 错误回调
    std::function<void(JSRuntime *, JSContext *, const std::string &, const std::string &, const std::string &)> jsErrorCallback_; // 错误回调
    std::function<void(JSRuntime *, JSContext *)> afterExecuteCallback_; // 执行完成后回调
    std::function<void(JSRuntime *, JSContext *)> beforeReleaseCallback_; // 资源释放前回调
    std::function<void(JSRuntime *, JSContext *)> afterContextCreateCallback_; // 上下文创建后回调
    std::function<void(JSRuntime *)> afterRuntimeCreateCallback_; // 上下文创建后回调
    bool debugEnabled_ = false; // 调试模式开关
    std::string entryFile_ = "main.js"; // 入口执行文件路径，默认为 main.js
    ExecutionMode executionMode_ = ExecutionMode::BINARY; // 执行模式，默认为二进制模式

    // 创建自定义上下文（供 Worker 线程调用）
    JSContext *createCustomContext(JSRuntime *rt) const;

    // 获取异常堆栈信息
    void getExceptionStack() const;

    // 触发错误回调
    void reportError(const std::string &msg) const;

    // 调试输出辅助函数
    void debugLog(const std::string &msg) const;

    // 读取文件内容到字符串
    std::string readFileToString(const std::string &filepath) const;
};
