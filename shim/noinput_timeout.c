/*
 * noinput-timeout-shim —— 由 sangfor-vdi-linux-patch 的可选功能 --disable-idle-logout 生成
 *
 * 背景
 * ----
 * 客户端 vdi_client 从服务端配置读 NoInputTimeout（字符串），经 atoi() 转成整数后
 * 存进 InputWatch 对象。值 > 0 时进入固定计时循环，到期主动向服务端发 logout，
 * 默认 1200 秒（20 分钟）。
 *
 * 该计时器并不响应任何真实输入：它每 5ms 轮询一次内部状态，但起点时间只在进入
 * 循环时记录一次，循环中从不更新，因此无论是否在操作都会准时到期。在远程桌面
 * 场景下（输入直接走 SPICE 通道，不经过本机任何事件源）尤其必然误踢。
 *
 * 做法
 * ----
 * 只拦截“解析 NoInputTimeout 的那一次 atoi 调用”，让它返回 0，使客户端跳过计时
 * 循环；进程内其余 atoi 调用原样透传，不影响其他任何逻辑。
 *
 * 调用点识别
 * ----------
 * 通过反汇编确定该调用的返回地址在 vdi_client 中的偏移为 0xd2ca8
 * （调用指令位于 0xd2ca3）。偏移按客户端版本校验，不匹配时本 shim 不生效，
 * 不会误伤其他调用。
 *
 * 适用性
 * ------
 * vdi_client 不带 setuid 位，动态链接器会尊重 LD_PRELOAD，因此由启动包装脚本
 * 注入即可。若客户端升级后该偏移变化，本 shim 自动失效（行为回到原厂默认）。
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>

/* 目标版本：com.sangfor.vdi 5.9.6.2154 */
#define EXPECTED_VERSION   "5.9.6.2154"
#define TARGET_RET_OFFSET  0xd2ca8UL
#define MATCH_WINDOW       0x40UL

static int (*real_atoi)(const char *);
static uintptr_t main_base;

static int find_main(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;
    /* dl_iterate_phdr 的第一项总是主程序 */
    main_base = (uintptr_t)info->dlpi_addr;
    return 1;
}

int atoi(const char *s) {
    if (!real_atoi) {
        real_atoi = dlsym(RTLD_NEXT, "atoi");
        if (!real_atoi) return 0;
    }
    if (!main_base) dl_iterate_phdr(find_main, NULL);

    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (main_base && caller > main_base) {
        uintptr_t off = caller - main_base;
        if (off <= TARGET_RET_OFFSET + MATCH_WINDOW &&
            off + MATCH_WINDOW >= TARGET_RET_OFFSET) {
            /* 命中 NoInputTimeout 的解析：归零，客户端不会启动空闲计时循环 */
            return 0;
        }
    }
    return real_atoi(s);
}