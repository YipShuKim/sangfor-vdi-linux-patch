# 深信服 VDI Linux 客户端补丁

**让深信服 aDesk / VDI 的 Linux 客户端在 Ubuntu 24.04+、Debian 12+、Linux Mint 22+、
Fedora 40+ 等新系统上装得上、连得通。**

官方客户端（`com.sangfor.vdi` 5.9.x）是在 Ubuntu 16.04/18.04 时代构建的，直接装到新系统上
会依次遇到**库版本不对 → 点图标没反应 → 登录框不出现 → 登录成功但打不开桌面**。
本项目按发行版拆成三个互相独立的补丁，每个都能单独执行、重复执行、完整撤销。

| 文件 | 面向 | 做什么 |
|---|---|---|
| `sangfor-vdi-ubuntu` | Ubuntu / Debian / Mint 等 deb 系 | **只做兼容库**：OpenLDAP 2.4、OpenSSL 1.0、Qt4、系统识别、setuid 程序的库路径。客户端用 apt 正常安装 |
| `sangfor-vdi-fedora` | Fedora / RHEL 系 | **从装客户端开始**：解官方 Ubuntu deb、执行其 `postinst`、装系统缺包，再打 Fedora 需要的兼容补丁 |
| `sangfor-vdi-keepalive` | 均可（可选） | 关闭客户端 20 分钟固定计时登出。独立文件，可单独开关 |

`shim/` 目录下是上述补丁编译用到的 C 源码（OpenLDAP 桩库、保活 shim），不需要手动处理。

## 快速开始

```bash
git clone <this-repo> && cd sangfor-vdi-linux-patch
chmod +x sangfor-vdi-*
```

### Ubuntu / Debian / Linux Mint

客户端用系统包管理器正常安装官方 deb（双击或 `sudo apt install ./com.sangfor.vdi_*.deb`），
然后：

```bash
sudo ./sangfor-vdi-ubuntu diagnose   # 只读检查
sudo ./sangfor-vdi-ubuntu apply      # 打兼容补丁
sudo ./sangfor-vdi-ubuntu verify     # 核对结果
```

之后从应用菜单点 **VDI** 即可。

### Fedora / RHEL 系

厂商只给 Ubuntu 的 `.deb`。准备好这个包（放在 `~/下载`、`~/Downloads`、当前目录或 `/tmp`，
或用 `--deb` 指定），一条命令做完安装与修复：

```bash
sudo ./sangfor-vdi-fedora apply --deb ~/下载/vdi-linux-client-x86_64-ubuntu.deb
sudo ./sangfor-vdi-fedora verify
```

`apply` 会依次：装系统缺的包（`dpkg`/`gcc`/`binutils`/`librtmp` 等）→ 解 deb 数据段
（只写 `/opt/apps` 与 `/usr/share/{applications,icons}`）→ 执行 deb 自带的 `postinst configure`
→ 处理 Fedora 上的兼容问题。只想装客户端时用 `sudo ./sangfor-vdi-fedora install --deb ...`。

> **不要用 `alien` 转出来的 `.rpm`**：alien 会丢掉 deb 的 `postinst`（真正的安装逻辑全在里面），
> 还会把依赖翻译成一堆 Fedora 上并不存在的 soname，装上去只会得到"文件在 `/opt` 里、
> 什么都没跑起来"的状态。

### 可选：关闭 20 分钟空闲自动登出

```bash
sudo ./sangfor-vdi-keepalive apply     # 编译 shim 并注入（重启客户端后生效）
sudo ./sangfor-vdi-keepalive verify
sudo ./sangfor-vdi-keepalive revert    # 恢复厂商默认
```

前置条件是**已经跑过某个主补丁的 `apply`**：它会把 `bin/` 下的原生二进制改成
「包装脚本 + `.real`」，保活补丁才有注入点。

## 三份补丁各做什么

### `sangfor-vdi-ubuntu`：五个兼容问题

| # | 现象 | 根因 | 处理方式 |
|---|---|---|---|
| 1 | `version 'OPENLDAP_2.4_2' not found` | 新系统只有 OpenLDAP 2.5/2.6，符号版本变成 `OPENLDAP_2.20x`；系统里的 `libldap_r-2.4.so.2` 往往只是指向 2.6 的软链 | 取一份真正的 OpenLDAP 2.4 放进 `compat/` |
| 2 | `libssl.so.1.0.0: no version information available`，或 `libssl.so.10 => not found` | 客户端多个二进制硬依赖 OpenSSL 1.0；新系统只有 OpenSSL 3 | 客户端自带 1.0.2k 时就地使用；缺失时从 Ubuntu 18.04 补 `libssl.so.10` |
| 3 | 点图标无反应；`vdi_super_agent` 反复重启；日志 `get system name failed` | `super_agent` 用 `grep -w ID /etc/os-release` 判断系统，只认 `ubuntu/kylin/uos/centos`。`linuxmint`/`debian` 会让它 `exit(255)`，不创建 `super_agent.sock`，GUI 随即判定"另一个登录窗口在运行"而退出 | 只给 `vdi_super_agent.service` 挂载一份 `ID=ubuntu` 的 os-release 副本（systemd `BindReadOnlyPaths`），系统其余部分不受影响 |
| 4 | 登录框不出现、CPU 100% | 依赖 Qt4。新发行版多数已移除；有的版本（如 rock-core PPA 的 `4.8.7+dfsg-7ubuntu4rock8`）会在 `QAbstractItemView::setItemDelegate → QMap` 迭代中死循环 | 使用 Ubuntu 官方构建的 Qt4 4.8.7，放到客户端 RPATH 里写死的 `/usr/local/Trolltech/Qt-4.8.7/lib` |
| 5 | 登录成功后卡在"正在连接会话"，最终报**检测到客户端组件连接超时** | `vdi_session` / `rapp_session` 带 **setuid 位**。动态链接器对 setuid 程序会忽略 `LD_LIBRARY_PATH`，包装脚本对它无效，子进程加载不到 OpenLDAP 2.4，70ms 内退出 | 单独建 `compat/ldso/`，只放不与系统同名冲突的库，通过 `/etc/ld.so.conf.d/sangfor-vdi-compat.conf` 加入默认搜索路径 |

### `sangfor-vdi-fedora`：Fedora 上额外踩到的坑

除上面五个之外，`sangfor-vdi-fedora` 还要处理这些（都在实测中确认过）：

| 问题 | 现象 / 根因 | 处理 |
|---|---|---|
| Fedora 的 OpenLDAP 2.4 没有该版本符号 | `openldap-2.4.59` 文件名齐全，但 `readelf -V` 里没有 `OPENLDAP_2.4_2` 定义；换成 Debian 的真实库又会牵出 Heimdal 一整套（`libgssapi.so.3`、`libkrb5.so.26`…） | 编译一个**桩库**：只导出 `libcurl` 实际引用的 23 个符号并打上 `OPENLDAP_2.4_2`。客户端自身不调用 LDAP（已逐个 objdump 核对） |
| `libidn.so.11` 缺失 | Fedora / Ubuntu 24.04 只剩 `libidn2` | 从 Ubuntu 18.04 的 `libidn11` 补一份 |
| `librtmp.so.1` 缺失 | 客户端直播/截图相关组件依赖 | 装系统包 `librtmp`（避免把第三方构建塞进全局链接路径） |
| `pidof` 匹配不到 `vdi_super_agent` | `vdi_webui_agent.sh` 用 `pidof vdi_super_agent` 判前置进程；Fedora 的 procps-ng 按 `argv[0]` 匹配，包装脚本执行 `xxx.real` 就永远等不到 | 包装脚本用 `exec -a` 保留原始 `argv[0]` |
| `is_installing_depend` 残留 | `postinst` 创建该标记，正常由 `install_depend.sh` 完成后删除；该脚本只认 Ubuntu/Kylin/UOS，在 Fedora 上永远不删。客户端读到此文件就认为依赖不完整（`CHECK_VER` 返回 `success:false`，日志 `client update! quit client and wait to start`） | 依赖齐备后自动清除 |
| `DISPLAY` 被算成 `<本机IP>:0` | GNOME 的 XWayland 只监听 unix socket，TCP 形式连不上 → `vdi_tray: cannot connect to X server <ip>` → 托盘死 → 客户端判定 `Session start process was canceled` 并退回资源列表 | 包装脚本检测到 `/tmp/.X11-unix/X0` 时把 IP 形式改写成 `:0`，并补 `XAUTHORITY=/run/user/<uid>/.mutter-Xwaylandauth.*` |

### `sangfor-vdi-keepalive`：关掉 20 分钟固定计时登出

客户端读取服务端下发的 `NoInputTimeout`，值大于 0 就进入**固定计时循环**，到期主动 logout
（默认 1200 秒）。这个"空闲"判定是失效的：

- 它每 5ms 轮询内部状态，但**起点时间只在进入循环时记录一次，循环中从不更新**
- 实测 1200.011s / 1200.499s，**没有任何重置迹象**
- 把用户加入 `input` 组、让进程能读 `/dev/input` 设备节点，结果完全相同
- 向 `/dev/input` 注入 inotify 事件 5 次，计时器毫无反应

远程桌面场景下必然误踢：键盘鼠标走 SPICE 通道直达虚拟机，不经过本机任何事件源。
（同一客户端在 Ubuntu 18.04 上不踢，是因为那边服务端下发的就是 0，循环根本没启动。）

实现：`shim/noinput_timeout.c` 编译成 `LD_PRELOAD` 库，**只拦截"解析 `NoInputTimeout`
的那一次 `atoi` 调用**（按返回地址偏移 `0xd2ca8` 精确匹配），返回 0，客户端便跳过计时循环；
进程内其余 `atoi` 原样透传。只注入 `bin/vdi_client` 的包装脚本，其它 16 个可执行文件不受影响。

- **偏移与客户端版本绑定**（在 `5.9.6.2154` 上验证）。客户端升级后若偏移变化，shim 不命中，
  行为回到厂商默认——**不会更糟，只是失效**，`apply` 会打印版本不匹配的警告。
- 启用后**会弱化组织的信息安全策略**（防止无人看管时桌面继续在线）。启用前请确认符合
  所在机构的规定；更合规的做法是让管理员在 VDC 控制台调大或关掉该策略值。

## 命令与选项

三个脚本的参数一致：`-y/--yes` 不询问、`--dry-run` 只打印、`-v/--verbose` 调试信息、`-h/--help`。

| `sangfor-vdi-ubuntu` | 作用 |
|---|---|
| `diagnose` | 只读检查：环境、各项依赖、setuid 程序、链接结果 |
| `apply` | 应用兼容补丁，缺失的兼容库自动下载（可重复执行） |
| `verify` | 校验：`ldd` 无缺失、服务状态、setuid 白名单是否到位 |
| `revert` | 撤销全部改动 |

额外选项：`--force-qt` 强制使用内置 Qt4、`--skip-qt` 不处理 Qt4。

| `sangfor-vdi-fedora` | 作用 |
|---|---|
| `install` | 只装客户端：找/解 deb、跑 `postinst`、装系统缺包 |
| `apply` | 未安装则先安装，然后打全部兼容补丁 |
| `diagnose` / `verify` / `revert` | 同上；`revert` **不卸载客户端** |

额外选项：`--deb <文件>` 指定客户端 deb。

| `sangfor-vdi-keepalive` | 作用 |
|---|---|
| `apply` | 编译 shim 并把 `LD_PRELOAD` 注入 `bin/vdi_client` 包装脚本 |
| `verify` | 检查 shim、注入范围、运行中的客户端是否已加载 |
| `revert` | 移除注入并删除 shim |

## 兼容库从哪来

默认从 Ubuntu 18.04（bionic）归档取：它是最后一个同时提供 OpenLDAP 2.4、OpenSSL 1.0、
官方 Qt4 4.8.7 和 `libaudio2` 的 LTS，且构建于 glibc 2.27，在所有现代 glibc 上都能运行。

内网环境可以整体指向镜像：

```bash
sudo BIONIC_BASE=http://mirror.example.com/ubuntu/pool ./sangfor-vdi-ubuntu apply
```

也可按包单独覆盖：`OPENLDAP24_DEB_BASE`、`OPENSSL10_DEB_BASE`、`QT4_DEB_BASE`、
`LIBAUDIO2_DEB_BASE`、`LIBIDN11_DEB_BASE`。

### OpenLDAP 2.4：真实库与桩库

补丁会先尝试本机软件源（deb 系取 `libldap-2.4-2`），并**校验它真的导出 `OPENLDAP_2.4_2`
符号版本**——Fedora 的 `openldap-2.4.59` 只是用了该版本符号、并未定义，所以不合格。

本机包不合格时回落到 Ubuntu 18.04 的真实库。如果真实库的**依赖链**在本系统上仍无法满足
（Fedora 只有 MIT krb5 和 `libsasl2.so.3`，而 Debian 的 libldap 要 Heimdal `libgssapi.so.3`
和 `libsasl2.so.2`，继续追会牵出一整串加密库），改为构建**桩库**：

- 只导出 `libcurl` 实际引用的 23 个符号，并打上 `OPENLDAP_2.4_2` 版本号
- 源码见 `shim/ldap24_stub.c`，被调用时一律返回失败，不伪造成功
- 依据：客户端**没有任何二进制直接调用 LDAP**；LDAP 仅由 `libcurl` 的 `ldap://` 协议支持
  使用，而 VDI 客户端只走 https

因此桩库只解决动态链接，不提供 LDAP 功能——在 VDI 场景下没有实际损失。

## 补丁做了什么

```
/usr/local/sangfor/vdiclient/
├── compat/                     # 旧 ABI 兼容库（OpenLDAP 2.4 / OpenSSL 1.0 / libaudio2 / libidn1）
│   ├── libldap_r-2.4.so.2      # 真实库或桩库
│   ├── liblber-2.4.so.2
│   ├── libssl.so.10, libcrypto.so.10
│   ├── libaudio.so.2, libidn.so.11
│   ├── os-release              # ID=ubuntu，仅供 super_agent 使用
│   ├── ldso/                   # setuid 程序专用：只放无冲突 SONAME 的库
│   └── shim/                   # 仅保活补丁：libnoinput_timeout.so
├── bin/
│   ├── vdi_local_client        # 包装脚本：设置 LD_LIBRARY_PATH 后 exec 真身
│   └── vdi_local_client.real   # 原二进制
└── lib/                        # 客户端自带库，不改动

/usr/local/Trolltech/Qt-4.8.7/lib/    # 官方 Qt4 4.8.7（如果系统 Qt4 不可用）
/etc/systemd/system/vdi_super_agent.service.d/compat-os-release.conf
/etc/ld.so.conf.d/sangfor-vdi-compat.conf   # 指向 compat/ldso（setuid 程序用）
```

包装脚本放在**原文件名**上、真身改名 `.real`，是因为客户端内部有多处硬编码调用
`/usr/local/sangfor/vdiclient/bin/...`（托盘、`desktop_tray.sh`、升级脚本、
`super_agent` 拉起子进程等）。这样无论从哪个入口启动都会带上兼容库，桌面快捷方式也保持原样。

### 为什么 setuid 程序要单独处理

`vdi_session` 与 `rapp_session` 带 `-rwsr-xr-x` 权限，客户端打开桌面时会 fork+exec 它们。
按 glibc 的安全约定，**setuid 程序启动时会忽略 `LD_LIBRARY_PATH`/`LD_PRELOAD`**，所以包装
脚本对它不起作用——子进程加载不到旧库就立刻退出，表现为"检测到客户端组件连接超时"。

补丁只把没有同名的库复制到独立目录 `compat/ldso/`，再用一个专用 `ld.so.conf.d` 文件挂进去。
系统的 OpenLDAP 2.6 提供的 SONAME 是 `libldap.so.2`，**与它们不重名**，不会遮蔽任何系统库；
`verify` 也会检查这一点，发现与已有系统库同名时报错。`revert` 会删除该配置并重新 `ldconfig`。

## 常见问题

**`apply` 之后从桌面图标启动，登录框正常，但打开桌面报"组件连接超时"？**
先看 `verify` 的最后几行。如果提示 setuid 程序缺少 `ld.so` 白名单，说明这一步没成功；
`/etc/ld.so.conf.d/sangfor-vdi-compat.conf` 是否写入并执行过 `ldconfig` 是关键。

**升级客户端后又要重新打补丁？**
是。客户端升级会覆盖 `bin/` 下的文件，重跑一次主补丁的 `apply` 即可（幂等）。重装后 `bin/`
里是原生二进制、旧的 `.real` 还在，`apply` 会识别并丢弃这些过期副本，不会把客户端降级。

**一长串 `no version information available` 要处理吗？**
不用。这是客户端内置 OpenSSL 1.0.2k 没有符号版本表导致的提示，库本身能正常加载。

**能否完全避免修改系统目录？**
前 4 项可以；第 5 项（setuid 程序）不能。动态链接器对 setuid 程序会忽略
`LD_LIBRARY_PATH`，只能让库出现在 ld.so 默认搜索路径里。补丁用专用 `ld.so.conf.d` 文件
把影响面压到最小，`revert` 可完整撤销。

**Fedora 上 USB 重定向能用吗？**
不能。客户端为 USB 重定向准备了预编译内核模块，但只覆盖 Kylin 与 Ubuntu 5.4/5.10 的内核；
Fedora 内核模块名是 `usbip_core`，与其预期不符。桌面连接本身不受影响。可停用它避免反复重启：

```bash
sudo systemctl disable --now vdi_usbipd
```

**Wayland 会话能用吗？**
能。客户端是 Qt4/X11 程序，通过 XWayland 运行；实测 Fedora 45（默认 Wayland）下登录框正常
显示。仅有一条 `QGtkStyle could not resolve GTK` 的无害提示。

**登录时报"登录异常，请检查网络和服务器地址"？**
这个提示名不副实，实际与网络无关。两个已确认的原因：

1. **`vdi_webui_agent` 没起来**（登录前的请求都发往 `127.0.0.1:31113`）。它由
   `vdi_webui_agent.sh` 拉起，而该脚本用 `pidof vdi_super_agent` 判断前置进程；Fedora 的
   procps-ng 按 `argv[0]` 匹配，包装脚本执行 `xxx.real` 就找不到。自查：
   `pidof vdi_super_agent && cat /run/sangfor/vdiclient/vdi_webui_agent.listen_port`
2. **`/etc/sangfor/vdiclient/is_installing_depend` 残留**。见上表，`sangfor-vdi-fedora`
   在依赖齐备后会自动清除。

**安装日志里 `vdi_session_conf: 是一个目录` 要紧吗？**
不要紧，是厂商自己的 bug：`postinst` 一处把 `/etc/sangfor/vdiclient/vdi_session_conf` 当目录
创建，另一处又把它当文件写入 `DISPLAY=...`，所以那句报错每次安装都会出现。DISPLAY 的实际
取值由启动包装脚本兜底，不影响登录。同理，`postinst` 开头三行 `vdi-install.log: 没有那个文件`
也只是它先写日志、后建目录的顺序问题，本补丁会预先建好目录。

**点开桌面后界面退回资源列表、进不去桌面？**
日志里若出现 `Session start process was canceled!`，通常是 **`vdi_tray` 先死掉**导致的连锁
反应。查日志中是否有 `cannot connect to X server <本机IP>`，那是 `DISPLAY` 被算成了 TCP 形式，
`sangfor-vdi-fedora` 的包装脚本会兜底改写为 `:0`。

`vdi_tray` 是 Qt4 的 `QSystemTrayIcon`，需要 XEmbed 系统托盘。GNOME 没有系统托盘，必要时
可装一个托盘宿主（如 `stalonetray --geometry 1x1+0+0 &`）。是否必需因桌面环境而异，先不加，
真出现托盘问题再装。

**GNOME 的"全部应用"里找不到客户端？**
客户端自带的 `.desktop` 有两个问题：末尾**没有换行符**，且 `Categories=Application;Development;`
会让 GNOME 45+ 把它收进「开发」文件夹而不是顶层。重写为规范格式即可：

```bash
sudo tee /usr/share/applications/com.sangfor.vdi.desktop >/dev/null <<'EOF'
[Desktop Entry]
Type=Application
Name=VDI
Name[zh_CN]=VDI 客户端
Exec=/usr/local/sangfor/vdiclient/bin/vdi_local_client
Icon=com.sangfor.vdi
Terminal=false
StartupNotify=true
StartupWMClass=vdi_local_client.real
Categories=Network;
Keywords=vdi;sangfor;adesk;desktop;remote;桌面;远程;
EOF
sudo gtk-update-icon-cache -f -t /usr/share/icons/hicolor
```

注意 GNOME 默认不在桌面上显示任何图标（需要 Desktop Icons NG 扩展），要找的是"全部应用"
或 Dock；固定到 Dock：右键图标 →「固定到 Dash」。

## 已知限制

- **已验证环境**：Linux Mint 22.3（Ubuntu 24.04 用户空间）与 Fedora 45，均为 x86_64。
  其他发行版应能工作但未逐一验证，首次使用建议先 `diagnose` 再 `apply`。
- Fedora 上 USB 重定向不可用（见上）。
- 如果系统里装了有缺陷的 Qt4（如 rock-core），补丁只是绕开它（RPATH 优先加载内置版本），
  不会卸载系统包；其他依赖该系统 Qt4 的程序仍可能受影响。
- `libaudio2` 是新系统普遍缺失的 Qt4 NAS 音频后端，下载失败不影响启动，只多一条链接警告。
- `ld.so.conf.d` 是唯一触及系统目录的改动，只引入 `compat/ldso/`，其中 SONAME 在系统
  ld.so 缓存中原本不存在，不改变任何现有程序的链接结果；`revert` 完整回滚。
- 在无符号版本的 OpenLDAP 上会退化为桩库，此时客户端不具备 LDAP 功能（VDI 用不到）。
- 仅适配 glibc 系（Debian/Ubuntu/Fedora 等），不适用于 musl 发行版（Alpine 等）。
- 保活 shim 与客户端版本绑定，升级后自动失效（回到厂商默认行为）。

## 免责声明

本项目与深信服科技无关联，为社区经验整理。默认行为只调整**本机客户端运行环境**，不修改
客户端鉴权与网络接入策略。

可选的 `sangfor-vdi-keepalive` **会让客户端的空闲自动登出不再生效**，这属于对厂商安全设计的
改动，可能不符合你所在机构的信息安全规定。请在使用前确认合规性，并优先考虑通过 VDC 管理端
调整该策略。

建议优先向厂商反馈并获取官方适配新系统的客户端；本项目适合作为过渡方案。

## 验证记录

Linux Mint 22.3（Ubuntu 24.04 用户空间，x86_64）：

- 应用后点应用菜单图标，登录框正常显示，CPU 稳定在 1–2%
- `/proc/<pid>/maps` 确认加载的是 `compat/` 与 `/usr/local/Trolltech/Qt-4.8.7/lib` 下的库
- `vdi_super_agent`（systemd）与 `vdi_webui_agent`（登录自启）均在线，`listen_port` 正常
- 17 个客户端二进制 `ldd` 无 `not found`
- 完整登录并打开桌面：`vdi_session`（保持 setuid，`/proc/<pid>/status` 显示 euid 为 0）
  以 `vdi_session.real -i <sessid> -s 0` 启动后持续运行，X 窗口
  `[<domain-uid> - <desk-name>] (Vdi Session)` 正常显示远程桌面
- 系统侧抽查：`curl` 仍链接 `libldap.so.2`（2.6），未被兼容库替换

Fedora Linux 45（Workstation，x86_64，kernel 7.2.15，Wayland 会话）：

- 用 deb 原生方式安装（解数据 + 跑 `postinst configure`），得到客户端目录、三个 systemd 单元
  与桌面项；`alien` 转出的 RPM 因缺少 scriptlet 不可用
- `apply` 后 `vdi_super_agent` 转为 active 并创建 `vdi_super_agent.sock`
- `vdi_local_client` 在 Wayland/XWayland 下启动，登录框正常显示
- 17 个二进制 `ldd` 无 `not found`；仅剩 OpenSSL 1.0 无版本表的无害提示
- OpenLDAP 走桩库路径（Fedora 原生 2.4 无 `OPENLDAP_2.4_2` 版本定义）
- USB 重定向不可用，已停用 `vdi_usbipd`

### 空闲登出的排查数据

| 结论 | 依据 |
|---|---|
| 计时器不响应真实输入 | 1200.011s / 1200.499s 两次实测，误差极小且无重置 |
| 加 `input` 组无效 | 组权限已生效（进程 groups 含 995），仍准时登出 |
| 注入 inotify 事件无效 | 5 次触发 `/dev/input` 事件，计时器无变化 |
| 取值路径 | 反汇编：`NoInputTimeout → atoi (0xd2ca83) → 存入 0xa8 → >0 才进入计时循环` |
| 同客户端在 Ubuntu 18.04 不踢 | 日志显示其 `InputWatch` 无 `end running`，即服务端下发值为 0 |
| shim 命中验证 | 诊断版实测打印 `atoi("1200") from base+0xd2ca8` |
| 最终效果 | 启用 shim 后会话持续 23 分钟以上、无 `InputWatch` 超时记录、无 logout |

## 从单脚本版本迁移

早期版本是单个 `sangfor-vdi-linux-patch`，把 Ubuntu 与 Fedora 的处理揉在一起，并把保活做成
`--disable-idle-logout` 选项。现在拆成三个文件：

| 旧 | 新 |
|---|---|
| `sangfor-vdi-linux-patch apply`（deb 系） | `sangfor-vdi-ubuntu apply` |
| `sangfor-vdi-linux-patch apply`（rpm 系） | `sangfor-vdi-fedora apply --deb <deb>` |
| `sangfor-vdi-linux-patch apply --disable-idle-logout` | 主补丁 `apply` + `sangfor-vdi-keepalive apply` |

旧版部署过的 `compat/shim/libnoinput_timeout.so` 会留在原地；想沿用保活就运行
`sangfor-vdi-keepalive apply`（它会重建并重新注入），想恢复默认就运行 `... revert`。