//
// Created by Li on 2025/11/27.
//


#include <cstdio>
#include <iostream>
#include "QjsBinaryCodeExecutor.h"

int main(int argc, char **argv) {
    printf("argc = %d\n", argc);


    for (int i = 1; i < 20; i++) {
        std::cout << "第 " << i << " 次执行" << std::endl;

        // 创建执行器
        QjsBinaryCodeExecutor executor;

        executor.setDebugMode(false);
        executor.setEntryFile("main.js");
        executor.setExecutionMode(ExecutionMode::JS);

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
