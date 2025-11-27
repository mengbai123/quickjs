// QjsBinaryCodeExecutor.cpp
#include "QjsBinaryCodeExecutor.h"

// 包含 QuickJS 头文件（只在 .cpp 中包含，实现接口与实现的分离）
#include <quickjs-libc.h>
#include <cstdio>
#include <cstring>
#include <iostream>

// 构造函数：初始化成员变量
QjsBinaryCodeExecutor::QjsBinaryCodeExecutor() {
    // 所有成员已在声明时初始化，这里不需要额外操作
}

// 析构函数：释放 JS 运行时和上下文
QjsBinaryCodeExecutor::~QjsBinaryCodeExecutor() {
    if (beforeReleaseCallback_) {
        beforeReleaseCallback_(runtime_, context_);
    }
    if (context_)
        JS_FreeContext(context_);
    if (runtime_)
        JS_FreeRuntime(runtime_);
}

// 从文件加载模块（复刻 qjs_bc.c 的加载逻辑）
void QjsBinaryCodeExecutor::loadModulesFromFile(const std::string &filename) {
    debugLog("正在加载模块文件: " + filename);

    FILE *f = fopen(filename.c_str(), "rb");
    if (!f) {
        reportError("无法打开文件: " + filename);
        return;
    }

    modules_.clear(); // 清空已有模块

    int module_index = 0;
    while (true) {
        // 分别读取 load_only (1字节) 和 data_length (8字节)
        uint8_t load_only;
        uint64_t data_length;

        // 先读取 load_only 标志
        if (fread(&load_only, 1, 1, f) != 1)
            break; // EOF，正常结束

        // 再读取数据长度
        if (fread(&data_length, sizeof(uint64_t), 1, f) != 1) {
            fclose(f);
            std::cerr << "模块头部不完整: module #" + std::to_string(module_index) << std::endl;
            return;
        }

        debugLog("load_only=" + std::to_string(load_only) + ", size=" + std::to_string(data_length) + " 字节");

        // 检查数据长度是否合理（防止错误的文件格式导致分配巨大内存）
        constexpr uint64_t MAX_MODULE_SIZE = 100 * 1024 * 1024; // 100MB
        if (data_length == 0 || data_length > MAX_MODULE_SIZE) {
            fclose(f);
            std::cerr << "模块大小异常: " + std::to_string(data_length) + " 字节（最大允许 " + std::to_string(MAX_MODULE_SIZE) +
                    " 字节）" << std::endl;

            return;
        }

        // 读取模块数据
        std::vector<uint8_t> data(data_length);
        if (fread(data.data(), 1, data_length, f) != data_length) {
            fclose(f);
            std::cout << "模块数据不完整: 期望 " + std::to_string(data_length) + " 字节" << std::endl;
            return;
        }

        // 添加到模块列表
        modules_.push_back({load_only != 0, std::move(data)});
        module_index++;
    }

    fclose(f);
    debugLog("加载完成: 共 " + std::to_string(module_index) + " 个模块");
}

// 静态回调函数：QuickJS 运行时调用的入口
// 参数：rt=JSRuntime，userdata=调用时传入的 this 指针
JSContext *QjsBinaryCodeExecutor::workerContextCallback(JSRuntime *rt, void *userdata) {
    // 将 userdata 转换回 QjsBinaryCodeExecutor* 指针
    auto *executor = static_cast<QjsBinaryCodeExecutor *>(userdata);
    // 调用成员函数完成实际工作
    return executor->createCustomContext(rt);
}

// 创建自定义上下文（供 Worker 线程调用）
JSContext *QjsBinaryCodeExecutor::createCustomContext(JSRuntime *rt) const {
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx)
        return nullptr;

    if (executionMode_ == ExecutionMode::BINARY) {
        // 预加载所有 load_only=1 的模块
        for (const auto &mod: modules_) {
            if (mod.load_only) {
                js_std_eval_binary_bool(ctx, mod.data.data(), mod.data.size(), true);
            }
        }
    }

    // 触发上下文创建后回调
    if (afterContextCreateCallback_) {
        debugLog("执行 afterContextCreate 回调...");
        afterContextCreateCallback_(rt, ctx);
    }

    return ctx;
}


// 获取异常堆栈信息
void QjsBinaryCodeExecutor::getExceptionStack() const {
    JSValue exception = JS_GetException(context_);
    const char *err_cstr = JS_ToCString(context_, exception);
    if (!err_cstr) {
        JS_FreeValue(context_, exception);
        return;
    }

    std::string result;
    if (JS_IsError(exception)) {
        JSValue name_val = JS_GetPropertyStr(context_, exception, "name");
        JSValue message_val = JS_GetPropertyStr(context_, exception, "message");
        JSValue stack_val = JS_GetPropertyStr(context_, exception, "stack");

        const char *name_cstr = JS_ToCString(context_, name_val);
        const char *message_cstr = JS_ToCString(context_, message_val);
        const char *stack_cstr = JS_ToCString(context_, stack_val);

        if (jsErrorCallback_) {
            jsErrorCallback_(runtime_, context_, name_cstr ? name_cstr : "", message_cstr ? message_cstr : "",
                             stack_cstr ? stack_cstr : "");
        }

        JS_FreeCString(context_, name_cstr);
        JS_FreeCString(context_, message_cstr);
        JS_FreeCString(context_, stack_cstr);

        JS_FreeValue(context_, name_val);
        JS_FreeValue(context_, message_val);
        JS_FreeValue(context_, stack_val);
    }

    JS_FreeCString(context_, err_cstr);
    JS_FreeValue(context_, exception);
}

// 触发错误回调
void QjsBinaryCodeExecutor::reportError(const std::string &msg) const {
    if (errorCallback_) {
        errorCallback_(runtime_, context_, msg);
    } else {
        fprintf(stderr, "[错误] %s\n", msg.c_str());
    }
}

// 执行主流程
int QjsBinaryCodeExecutor::execute() {
    debugLog("开始执行...");
    debugLog("执行模式: " + std::string(executionMode_ == ExecutionMode::BINARY ? "二进制字节码" : "JS源代码"));

    if (!entryFile_.empty()) {
        debugLog("入口文件: " + entryFile_);
    }

    // 加载二进制文件的模块
    if (executionMode_ == ExecutionMode::BINARY) {
        loadModulesFromFile(entryFile_);
    }

    // 1. 创建 JSRuntime
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        reportError("创建 JSRuntime 失败");
        return -1;
    }

    if (afterRuntimeCreateCallback_) {
        afterRuntimeCreateCallback_(runtime_);
    }

    // 初始化标准库处理器
    js_std_init_handlers(runtime_);

    // 2. 设置 Worker 创建回调（传递 this 指针）
    // js_std_set_worker_new_context_func(workerContextCallback, this);

    // 设置模块加载器
    JS_SetModuleLoaderFunc(runtime_, nullptr, js_module_loader, nullptr);

    // 3. 创建 JSContext
    context_ = createCustomContext(runtime_);
    if (!context_) {
        reportError("创建 JSContext 失败");
        return -1;
    }

    // 如果指定了入口文件，优先使用它
    debugLog("执行指定入口文件: " + entryFile_);

    if (executionMode_ == ExecutionMode::JS) {
        // JS 源代码模式
        std::string jsCode = readFileToString(entryFile_);
        debugLog("JS 源代码: " + jsCode);
        const JSValue runResult = JS_Eval(context_, jsCode.c_str(), strlen(jsCode.c_str()), entryFile_.c_str(),
                                          JS_EVAL_TYPE_MODULE);
        if (JS_HasException(context_)) {
            debugLog("has exception!");
            getExceptionStack();
        } else {
            const JSValue pr = JS_PromiseResult(context_, runResult);
            if (JS_IsException(pr) || JS_IsError(pr)) {
                // 主动抛出一个 getExceptionStack() 可以捕获
                JS_Throw(context_, pr);
                getExceptionStack();
            } else {
                // Promise 正常 resolved，释放 如果throw了不用释放
                JS_FreeValue(context_, pr);
            }
        }
        JS_FreeValue(context_, runResult);
    } else {
        // 5. 执行入口模块（第一个 load_only=0 的模块）
        bool has_entry = false;
        // 预加载所有 load_only=1 的模块（供 Worker 使用）
        for (const auto &module: modules_) {
            if (!module.load_only) {
                // 执行main文件 通常main会在二进制文件最后 并且是唯一的load_only为false的
                bool runSuccess = js_std_eval_binary_bool(context_, module.data.data(), module.data.size(),
                                                          module.load_only);
                if (!runSuccess) {
                    getExceptionStack();
                }
                has_entry = true;
            }
        }
        if (!has_entry) {
            reportError("未找到入口模块（load_only=0）或未指定入口文件");
        }
    }

    // 6. 运行事件循环（处理异步操作）
    debugLog("进入事件循环...");
    int ret = js_std_loop(context_);

    debugLog("执行完成，返回值: " + std::to_string(ret));

    // 7. 触发 afterExecute 回调
    if (afterExecuteCallback_) {
        afterExecuteCallback_(runtime_, context_);
    }

    return ret;
}

// 调试输出辅助函数
void QjsBinaryCodeExecutor::debugLog(const std::string &msg) const {
    if (debugEnabled_) {
        printf("[DEBUG] %s\n", msg.c_str());
    }
}

// 读取文件内容到字符串
std::string QjsBinaryCodeExecutor::readFileToString(const std::string &filepath) const {
    debugLog("正在读取文件: " + filepath);

    FILE *f = fopen(filepath.c_str(), "rb");
    if (!f) {
        return ""; // 文件打开失败，返回空字符串
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return ""; // 文件为空或获取大小失败
    }

    // 读取文件内容
    std::string content(file_size, '\0');
    size_t bytes_read = fread(&content[0], 1, file_size, f);
    fclose(f);

    if (bytes_read != static_cast<size_t>(file_size)) {
        debugLog("警告: 读取的字节数与文件大小不匹配");
        content.resize(bytes_read); // 调整为实际读取的大小
    }

    debugLog("成功读取 " + std::to_string(bytes_read) + " 字节");
    return content;
}
