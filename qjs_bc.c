//
// Created by Li on 2025/11/26.
//


/**
 * @file qjs_load_binary_v2.c
 * @brief QuickJS二进制字节码加载器（支持Worker）
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "quickjs-libc.h"

// 全局存储所有模块，供JS_NewCustomContext访问
typedef struct {
    bool load_only;
    uint64_t size;
    uint8_t *data;
} ModuleInfo;

static ModuleInfo *g_modules = NULL;
static int g_module_count = 0;

/**
 * @brief 从二进制文件加载所有模块到全局数组
 */
static int load_all_modules(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open binary file");
        return -1;
    }

    int capacity = 16;
    g_modules = malloc(sizeof(ModuleInfo) * capacity);
    if (!g_modules) {
        fclose(f);
        return -1;
    }

    g_module_count = 0;
    while (1) {
        uint8_t load_only;
        uint64_t data_length;

        if (fread(&load_only, 1, 1, f) != 1) break; // EOF

        if (fread(&data_length, sizeof(uint64_t), 1, f) != 1) {
            fprintf(stderr, "Error: Incomplete module header at #%d\n", g_module_count);
            goto error;
        }

        // 动态扩容
        if (g_module_count >= capacity) {
            capacity = capacity * 2;
            ModuleInfo *new_modules = realloc(g_modules, sizeof(ModuleInfo) * capacity);
            if (!new_modules) goto error;
            g_modules = new_modules;
        }

        // 读取数据
        g_modules[g_module_count].data = malloc(data_length);
        if (!g_modules[g_module_count].data) goto error;

        if (fread(g_modules[g_module_count].data, 1, data_length, f) != data_length) {
            fprintf(stderr, "Error: Failed to read %llu bytes for module #%d\n",
                    (unsigned long long) data_length, g_module_count);
            goto error;
        }

        g_modules[g_module_count].size = data_length;
        g_modules[g_module_count].load_only = load_only;
        g_module_count++;
    }

    printf("Loaded %d modules from '%s'\n", g_module_count, filename);
    fclose(f);
    return 0;

error:
    fclose(f);
    for (int i = 0; i < g_module_count; i++) {
        free(g_modules[i].data);
    }
    free(g_modules);
    g_modules = NULL;
    g_module_count = 0;
    return -1;
}

/**
 * @brief 清理全局模块数据
 */
static void free_all_modules(void) {
    if (!g_modules) return;
    for (int i = 0; i < g_module_count; i++) {
        free(g_modules[i].data);
    }
    free(g_modules);
    g_modules = NULL;
    g_module_count = 0;
}

/**
 * @brief 自定义上下文创建函数（Worker和主线程都会调用）
 *
 * 预加载所有load_only=1的模块到模块registry
 */
static JSContext *JS_NewCustomContext(JSRuntime *rt) {
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create JS context\n");
        return NULL;
    }

    // 为Worker预加载所有依赖模块
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i].load_only) {
            js_std_eval_binary_bool(ctx, g_modules[i].data, g_modules[i].size, true);
        }
    }

    return ctx;
}


/**
 * @brief 获取JS异常的完整信息（错误消息 + 堆栈）
 *
 * 此函数替代 js_std_dump_error() 的打印行为，
 * 将错误信息拼接为单个字符串返回。
 *
 * @param ctx QuickJS 上下文
 * @return 动态分配的字符串（需调用 free() 释放），失败返回 NULL
 */
char* getExceptionStack(JSContext *ctx) {
    JSValue exception = JS_GetException(ctx);
    const char *err_cstr = JS_ToCString(ctx, exception);

    if (!err_cstr) {
        JS_FreeValue(ctx, exception);
        return strdup("Unknown error: failed to convert exception to string");
    }

    char *result = NULL;

    // 如果是 Error 对象，尝试获取堆栈
    if (JS_IsError(exception)) {
        JSValue stack_val = JS_GetPropertyStr(ctx, exception, "stack");
        if (!JS_IsUndefined(stack_val)) {
            const char *stack_cstr = JS_ToCString(ctx, stack_val);
            if (stack_cstr) {
                // 计算总长度：错误消息 + 换行符 + 堆栈 + 空终止符
                size_t total_len = strlen(err_cstr) + strlen(stack_cstr) + 2;
                result = malloc(total_len);
                if (result) {
                    snprintf(result, total_len, "%s\n%s", err_cstr, stack_cstr);
                }
                JS_FreeCString(ctx, stack_cstr);
            }
            JS_FreeValue(ctx, stack_val);
        }
    }

    // 非 Error 对象或没有堆栈时，仅返回错误消息
    if (!result) {
        result = strdup(err_cstr);
    }

    JS_FreeCString(ctx, err_cstr);
    JS_FreeValue(ctx, exception);

    return result;
}

int main(int argc, char **argv) {
    printf("----------- [ QJSC_BC START ] -----------\n");
    printf("qjs_bc=%d\n", argc);

    // 参数检查
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <binary-file> [script-args...]\n", argv[0]);
        return 1;
    }

    // 1. 首先加载所有模块到内存
    if (load_all_modules(argv[1]) < 0) {
        return 1;
    }

    // 2. 创建JS运行时
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "Error: Failed to create JS runtime\n");
        free_all_modules();
        return 1;
    }

    // 3. 初始化标准库和设置Worker创建函数
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(rt);
    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL);

    // 4. 创建主上下文（自动预加载load_only模块）
    JSContext *ctx = JS_NewCustomContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        free_all_modules();
        return 1;
    }

    // 5. 添加命令行参数（跳过程序名和二进制文件名）
    js_std_add_helpers(ctx, argc - 2, &argv[2]);

    // 6. 执行入口模块（load_only=0的模块）
    bool has_entry = false;
    for (int i = 0; i < g_module_count; i++) {
        if (!g_modules[i].load_only) {
            has_entry = true;
            printf("----------- [ main.js ] -----------\n");
            bool state = js_std_eval_binary_bool(ctx, g_modules[i].data, g_modules[i].size, 0);
            if (!state) {
                printf("----------- [ !state ] -----------\n");
                char* error_info = getExceptionStack(ctx);
                if (error_info) {
                    printf("%s\n", error_info);
                    free(error_info);
                }
            }
            printf("----------- [ main.js end ] -----------\n");
        }
    }

    if (!has_entry) {
        fprintf(stderr, "Warning: No entry module (load_only=0) found\n");
    }

    // 7. 运行事件循环
    int result = js_std_loop(ctx);
    if (result != 0) {
        js_std_dump_error(ctx);
    }

    // 8. 清理
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    free_all_modules();

    printf("----------- [ QJSC_BC END ] -----------\n");
    return result;
}
