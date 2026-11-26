/*
 * xkbcompat —— 让深信服 VDI 客户端在 XWayland 下正确翻译扩展键
 *
 * 根因：客户端沿用旧版 spice/gtk-vnc 的键盘类型探测逻辑：
 *     desc     = XkbGetKeyboard(dpy, ..., XkbUseCoreKbd);
 *     keycodes = XGetAtomName(dpy, desc->names->keycodes);
 *     if (strstr(keycodes, "evdev"))  -> 用 evdev 键盘表（扩展键正确）
 *     else                            -> 用 XT 回退表（方向键/Home/End 全错）
 *
 * XWayland 上 XkbGetKeyboard 直接返回 NULL（freedesktop #89240）；即使按
 * spice-gtk 2015 的改法用 XkbGetMap+XkbGetNames 重建，组件名 atom
 * （keycodes/symbols/types…）依然是 None，客户端 strstr 失败就退回破表 ——
 * 表现为本地正常、进虚拟机后 ↑↓←→/Home/End/Insert/Delete 错乱或丢失。
 *
 * 做法：只在 vdi_session / rapp_session 里（进程名匹配或 XKBCOMPAT_FORCE=1）
 *   1. XkbGetKeyboard 返回 NULL 时用 XkbGetMap+XkbGetNames 重建；
 *   2. 无论哪条路径，只要 desc 的 names->keycodes 为空，就强制设成 "evdev"
 *      （symbols/types/compat 也补成合理值），让客户端走 evdev 表。
 * 其它进程完全原样透传。
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/XKBlib.h>

typedef XkbDescPtr (*get_kbd_fn)(Display *, unsigned int, unsigned int);
typedef XkbDescPtr (*get_map_fn)(Display *, unsigned int, unsigned int);
typedef int (*get_names_fn)(Display *, unsigned int, XkbDescPtr);

static int g_active = -1;

static const char *self_name(void)
{
    static char buf[256];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    const char *p = strrchr(buf, '/');
    return p ? p + 1 : buf;
}

static void loghit(const char *msg)
{
    const char *lp = getenv("XKBCOMPAT_LOG");
    FILE *f = fopen(lp && *lp ? lp : "/tmp/xkbcompat.log", "a");
    if (f) { fprintf(f, "%s %s\n", self_name(), msg); fclose(f); }
}

static int active(void)
{
    if (g_active >= 0) return g_active;
    const char *e = getenv("XKBCOMPAT_FORCE");
    if (e && *e && *e != '0') { g_active = 1; return g_active; }
    const char *n = self_name();
    g_active = (!strcmp(n, "vdi_session") || !strcmp(n, "vdi_session.real") ||
                !strcmp(n, "rapp_session") || !strcmp(n, "rapp_session.real")) ? 1 : 0;
    return g_active;
}

/* 补齐组件名 atom，关键是 keycodes="evdev" */
static int fix_names(Display *dpy, XkbDescPtr d)
{
    XkbNamesPtr nm;
    int changed = 0;
    if (!d) return 0;
    if (!d->names) {
        d->names = calloc(1, sizeof(*d->names));
        if (!d->names) return 0;
    }
    nm = d->names;
    if (nm->keycodes == None) { nm->keycodes = XInternAtom(dpy, "evdev", False); changed = 1; }
    if (nm->symbols  == None) nm->symbols  = XInternAtom(dpy, "pc", False);
    if (nm->types    == None) nm->types    = XInternAtom(dpy, "complete", False);
    if (nm->compat   == None) nm->compat   = XInternAtom(dpy, "complete", False);
    if (nm->geometry == None) nm->geometry = XInternAtom(dpy, "pc(pc105)", False);
    return changed;
}

XkbDescPtr XkbGetKeyboard(Display *dpy, unsigned int which, unsigned int dev_spec)
{
    static get_kbd_fn real_get, real_map;
    static get_names_fn real_names;
    if (!real_get)   real_get   = (get_kbd_fn)dlsym(RTLD_NEXT, "XkbGetKeyboard");
    if (!real_map)   real_map   = (get_map_fn)dlsym(RTLD_NEXT, "XkbGetMap");
    if (!real_names) real_names = (get_names_fn)dlsym(RTLD_NEXT, "XkbGetNames");

    XkbDescPtr d = real_get ? real_get(dpy, which, dev_spec) : NULL;

    if (!active()) return d;

    if (!d && real_map) {
        d = real_map(dpy, XkbAllClientInfoMask, dev_spec);
        if (!d) { loghit("rebuild failed"); return NULL; }
        if (real_names) real_names(dpy, XkbAllNamesMask, d);
        loghit("rebuilt via XkbGetMap+XkbGetNames");
    }

    if (d) {
        if (fix_names(dpy, d)) loghit("forced keycodes atom = evdev");
        else loghit("names already ok");
    }
    return d;
}
