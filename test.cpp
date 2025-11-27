//
// Created by Li on 2025/11/27.
//

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <iostream>
#include "QjsBinaryCodeExecutor.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("argc = %d\n", argc);

    for (int i = 1; i < 2; i++) {
        std::cout << "第 " << i << " 次执行" << std::endl;

        // 创建执行器
        QjsBinaryCodeExecutor executor;
        executor.setXorSecret("QWEQWE");
        executor.setDebugMode(true);
        executor.setEntryFile("main.bc");
        executor.setExecutionMode(ExecutionMode::BINARY);

        executor.setLogCallback([](const std::string& log) {
            printf("%s\n", log.c_str());
        });

        executor.afterContextCreate([](JSRuntime *rt, JSContext *ctx) {
            // 添加自定义库和函数
            std::cout << "afterContextCreate" << std::endl;
        });

        executor.beforeRelease([](JSRuntime *rt, JSContext *ctx) {
            // 回收资源
            std::cout << "beforeRelease" << std::endl;
        });

        // 设置错误回调（可选）
        executor.onError([](JSRuntime *rt, JSContext *ctx, const std::string &err) {
            // 错误是否弹窗 打印
            std::cout << "onError " << err << std::endl;
        });
        executor.onJsError([](JSRuntime *rt, JSContext *ctx, const std::string &name, const std::string &msg,
                              const std::string &stack) {
            std::cerr << "====== [ onJsError ] ======" << std::endl;
            std::cerr << " name: " << name << std::endl;
            std::cerr << msg << std::endl << stack << std::endl;
        });

        executor.afterExecute([](JSRuntime *rt, JSContext *ctx) {
            // 回收资源
            std::cout << "afterExecute" << std::endl;
        });

        // // 加载并执行
        int result = executor.execute();

        std::cout << "程序退出码: " << result << std::endl;
    }


    return 0;
}
