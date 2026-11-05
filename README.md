# sangfor-vdi-linux-patch

**让深信服 VDI（aDesk）Linux 客户端在新版 Linux 上正常连接虚拟桌面。**

适用于 Ubuntu 24.04+、Debian 12+、Fedora 40+、Linux Mint 22+ 及基于它们的衍生版。

> 官方 Linux 客户端（`com.sangfor.vdi` 5.9.x）是在 Ubuntu 16.04/18.04、CentOS 7 时代构建的，
> 直接装到新系统上会遇到 5 个彼此独立的兼容问题，表现为：**启动报库版本错误 → 点图标无反应
> → 登录框不出现 → 登录成功但打不开桌面**。本补丁用一个脚本按顺序处理掉它们，可重复执行、
> 可完整撤销，不降级系统软件包。

## 快速开始

```bash
git clone <this-repo> && cd sangfor-vdi-linux-patch
chmod +x sangfor-vdi-linux-patch

sudo ./sangfor-vdi-linux-patch diagnose   # 先看状态，只读不改
sudo ./sangfor-vdi-linux-patch apply      # 打补丁
sudo ./sangfor-vdi-linux-patch verify     # 核对结果
```

之后从应用菜单点 **VDI** 即可。

## 五个问题

| # | 现象 | 根因 | 处理方式 |
|---|---|---|---|
| 1 | `version 'OPENLDAP_2.4_2' not found` | 新系统只有 OpenLDAP 2.5/2.6，符号版本变成 `OPENLDAP_2.20x`；系统里的 `libldap_r-2.4.so.2` 往往只是指向 2.6 的软链 | 取一份真正的 OpenLDAP 2.4，放进 `compat/` |
| 2 | `libssl.so.1.0.0: no version information available`，或 `libssl.so.10 => not found` | 客户端多个二进制硬依赖 OpenSSL 1.0；新系统只有 OpenSSL 3 | 客户端自带 1.0.2k 时就地使用；缺失时从 18.04 补 `libssl.so.10` |
| 3 | 点图标无反应；`vdi_super_agent` 反复重启；日志 `get system name failed` | `super_agent` 用 `grep -w ID /etc/os-release` 判断系统，只认 `ubuntu/kylin/uos/centos`。`linuxmint`/`debian`/`fedora` 会让它 `exit(255)`，不创建 `super_agent.sock`，GUI 随即判定"另一个登录窗口在运行"而退出 | 只给 `vdi_super_agent.service` 挂载一份 `ID=ubuntu` 的 os-release 副本（systemd `BindReadOnlyPaths`），系统其余部分不受影响 |
| 4 | 登录框不出现、CPU 100% | 依赖 Qt4。新发行版多数已移除；有的发行版（如 rock-core PPA 的 `4.8.7+dfsg-7ubuntu4rock8`）虽有但会在 `QAbstractItemView::setItemDelegate → QMap` 迭代中死循环 | 使用 Ubuntu 官方构建的 Qt4 4.8.7，放到客户端 RPATH 里写死的 `/usr/local/Trolltech/Qt-4.8.7/lib` |
| 5 | 登录成功后卡在"正在连接会话"，最终报**检测到客户端组件连接超时** | `vdi_session` / `rapp_session` 带 **setuid 位**，用作会话子进程。动态链接器对 setuid 程序会忽略 `LD_LIBRARY_PATH`，因此包装脚本对它无效，子进程加载不到 OpenLDAP 2.4，70ms 内退出 | 单独建 `compat/ldso/`，只放 3 个不与系统同名冲突的库，通过 `/etc/ld.so.conf.d/sangfor-vdi-compat.conf` 加入默认搜索路径 |

## 命令与选项

| 命令 | 作用 |
|---|---|
| `diagnose` | 只读检查：环境、各项依赖、setuid 程序、链接结果 |
| `apply` | 应用补丁，缺失的兼容库自动下载（可重复执行） |
| `verify` | 校验：`ldd` 无缺失、服务状态、setuid 白名单是否到位 |
| `revert` | 撤销全部改动，恢复原状 |

常用选项：`-y/--yes` 不询问 · `--dry-run` 只打印操作 · `--force-qt` 强制使用内置 Qt4 ·
`--skip-qt` 不处理 Qt4 · `--disable-idle-logout` 关闭 20 分钟空闲自动登出（可选，见下）。

## 可选功能：关闭 20 分钟空闲自动登出

默认**不启用**。加上 `--disable-idle-logout` 才会部署。

```bash
sudo ./sangfor-vdi-linux-patch apply --disable-idle-logout
# 不想要时去掉该参数重跑，会自动移除
sudo ./sangfor-vdi-linux-patch apply
```

### 为什么需要它

客户端会读取服务端下发的 `NoInputTimeout`，值大于 0 就启动一个**固定计时循环**，
到期主动向服务端发 logout（默认 1200 秒）。问题在于这个“空闲”判定是失效的：

- 它每 5ms 轮询一次内部状态，但**起点时间只在进入循环时记录一次，循环中从不更新**
- 计时精度实测为 1200.011 秒 / 1200.499 秒，**没有任何重置迹象**
- 即使把用户加入 `input` 组、让进程能读 `/dev/input` 设备节点，结果完全相同
- 向 `/dev/input` 注入 inotify 事件 5 次，计时器毫无反应

也就是说，在远程桌面场景下它**必然误踢**：你的键盘鼠标输入走 SPICE 通道直达虚拟机，
不经过本机任何事件源。反过来，同一客户端在 Ubuntu 18.04 上不会踢，是因为那边服务端
下发的是 0，循环根本没启动。

### 实现方式

`shim/noinput_timeout.c` 编译成一个 `LD_PRELOAD` 库，**只拦截“解析 NoInputTimeout 的那一次
`atoi` 调用”**（通过返回地址偏移 `0xd2ca8` 精确匹配），让它返回 0，客户端便跳过计时循环。
进程内其余 `atoi` 调用原样透传，其他 16 个可执行文件完全不受影响。

`vdi_client` 不带 setuid 位，动态链接器会尊重 `LD_PRELOAD`，因此由启动包装注入即可。

### 注意事项

- **偏移与客户端版本绑定**（已在 `5.9.6.2154` 上验证）。客户端升级后若偏移变化，shim
  不会命中，行为回到厂商默认——**不会更糟，只是失效**。`apply` 会打印版本不匹配的警告。
- 该 shim 让“空闲自动登出”不再生效，**这会弱化组织的信息安全策略**（防止无人看管时
  桌面继续在线）。启用前请确认符合所在机构的规定。
- 如果只想调整时长而不是完全关闭，最合规的做法是让管理员在 VDC 控制台修改
  该策略值。

## 补丁做了什么

```
/usr/local/sangfor/vdiclient/
├── compat/                     # 旧 ABI 兼容库（OpenLDAP 2.4 / OpenSSL 1.0 / libaudio2）
│   ├── libldap_r-2.4.so.2 → 2.10.x
│   ├── liblber-2.4.so.2
│   ├── libssl.so.10, libcrypto.so.10
│   ├── libaudio.so.2
│   ├── os-release              # ID=ubuntu，仅供 super_agent 使用
│   ├── ldso/                   # setuid 程序专用：只放 3 个无冲突 SONAME
│   └── shim/                   # 可选：libnoinput_timeout.so（关闭空闲登出）
├── bin/
│   ├── vdi_local_client        # 包装脚本：设置 LD_LIBRARY_PATH 后 exec 真身
│   └── vdi_local_client.real   # 原二进制
└── lib/                        # 客户端自带库，不改动

/usr/local/Trolltech/Qt-4.8.7/lib/    # 官方 Qt4 4.8.7（如果系统 Qt4 不可用）
/etc/systemd/system/vdi_super_agent.service.d/compat-os-release.conf
/etc/ld.so.conf.d/sangfor-vdi-compat.conf   # 指向 compat/ldso（setuid 程序用）
```

包装脚本之所以放在**原文件名**上、真身改名 `.real`，是因为客户端内部有多处硬编码调用
`/usr/local/sangfor/vdiclient/bin/...`（托盘、`desktop_tray.sh`、升级脚本、
`super_agent` 拉起子进程等）。这样无论从哪个入口启动，都会带上兼容库，桌面快捷方式也
保持官方原样。

### 为什么 setuid 程序要单独处理

`vdi_session` 与 `rapp_session` 带 `-rwsr-xr-x` 权限，客户端打开桌面时会 fork+exec 它们。
按 glibc 的安全约定，**setuid 程序启动时会忽略 `LD_LIBRARY_PATH` 等变量**，所以包装脚本
对它不起作用——子进程加载不到旧库就立刻退出，表现为"检测到客户端组件连接超时"。

补丁的做法是只把这 3 个库（`libldap_r-2.4.so.2`、`liblber-2.4.so.2`、`libaudio.so.2`）
复制到独立目录 `compat/ldso/`，再用一个专用 `ld.so.conf.d` 文件挂进去。选这 3 个是因为：
系统的 OpenLDAP 2.6 提供的 SONAME 是 `libldap.so.2`，**与它们不重名**，不会遮蔽任何
系统库；`verify` 里也会检查这一点，发现与已有系统库同名时会报错提示。

执行 `revert` 时会删除该配置文件并重新运行 `ldconfig`。

## 兼容库从哪来

默认从 Ubuntu 18.04（bionic）归档取：它是最后一个同时提供 OpenLDAP 2.4、OpenSSL 1.0、
官方 Qt4.8.7 和 `libaudio2` 的 LTS，且构建于 glibc 2.27，在所有现代 glibc 上都能运行。

内网环境可以整体指向镜像：

```bash
sudo BIONIC_BASE=http://mirror.example.com/ubuntu/pool ./sangfor-vdi-linux-patch apply
```

也可以按包单独覆盖：`OPENLDAP24_DEB_BASE`、`OPENSSL10_DEB_BASE`、`QT4_DEB_BASE`、
`LIBAUDIO2_DEB_BASE`、`LIBIDN11_DEB_BASE`、`OPENLDAP24_RPM_BASE`。

### OpenLDAP 2.4：真实库 与 桩库

补丁会先尝试本机软件源（Debian 系取 `libldap-2.4-2`，Fedora 取归档里的 RPM），
但**必须验证它真的导出 `OPENLDAP_2.4_2` 符号版本**——Fedora 的
`openldap-2.4.59` 虽然文件名齐全，却只是用了该版本符号、并未定义，所以不合格。

本机包不合格时回落到 Ubuntu 18.04 的真实库。如果这个真实库的**依赖链**在本系统上
仍无法满足（Fedora 只有 MIT krb5 和 `libsasl2.so.3`，而 Debian 的 libldap 要
Heimdal `libgssapi.so.3` 和 `libsasl2.so.2`，继续追下去会牵出一整串加密库），
补丁改为构建一个**桩库**：

- 只导出 `libcurl` 实际引用的 23 个符号，并打上 `OPENLDAP_2.4_2` 版本号
- 源码见 `shim/ldap24_stub.c`，被调用时一律返回失败，不伪造成功
- 依据：客户端**没有任何二进制直接调用 LDAP**（已逐个 objdump 核对）；LDAP 仅由
  `libcurl` 的 `ldap://` 协议支持使用，而 VDI 客户端只走 https

因此桩库只解决动态链接，不提供 LDAP 功能——在 VDI 场景下没有实际损失。

## 常见问题

**`apply` 之后从桌面图标启动，登录框正常，但打开桌面报"组件连接超时"？**
先看 `verify` 的最后几行。如果提示 setuid 程序缺少 `ld.so` 白名单，说明这一步没成功；
`/etc/ld.so.conf.d/sangfor-vdi-compat.conf` 是否写入并执行过 `ldconfig` 是关键。

**升级客户端后又要重新打补丁？**
是。`vdi_update` 会覆盖 `bin/` 下的文件，重跑一次 `apply` 即可（幂等，会先安全还原再重装）。

**一长串 `no version information available` 要处理吗？**
不用。这是客户端内置 OpenSSL 1.0.2k 没有符号版本表导致的提示，库本身能正常加载，
不影响功能。

**能否完全避免修改系统目录？**
前 4 项可以；第 5 项（setuid 程序）不能。动态链接器对 setuid 程序会忽略
`LD_LIBRARY_PATH`/`LD_PRELOAD`，只能让库出现在 ld.so 默认搜索路径里。补丁用专用
`ld.so.conf.d` 文件把影响面压到最小，`revert` 可完整撤销。

**想窗口化而不是全屏显示远程桌面？**
这是客户端自身行为（`vdi_session` 的显示参数），与本补丁无关。

**Fedora 安装客户端用哪个包？**
用官方 Ubuntu 的 `.deb`。**不要用 `alien` 转出来的 `.rpm`**：alien 会丢掉 deb 的
`postinst`（真正的安装逻辑全在里面），并把依赖翻译成一堆 Fedora 上不存在的 soname，
装上去只会得到"文件在 /opt 里、什么都没跑起来"的状态。正确做法是解出 deb 的数据
（只写 `/opt/apps`、`/usr/share/icons`、`/usr/share/applications`，不覆盖系统文件），
再执行 `postinst configure`。

**Fedora 上 USB 重定向能用吗？**
不能。客户端为 USB 重定向准备了预编译内核模块，但只覆盖 kylin 与 Ubuntu 5.4/5.10
的内核；Fedora 内核版本不同，模块无法匹配（Fedora 自带 usbip 模块的模块名是
`usbip_core`，与其预期不符）。桌面连接本身不受影响。可以在安装后停用它避免反复重启：

```bash
sudo systemctl disable --now vdi_usbipd
```

**Wayland 会话能用吗？**
能。客户端是 Qt4/X11 程序，通过 XWayland 运行；实测 Fedora 45（默认 Wayland）下
登录框正常显示。仅有一条 `QGtkStyle could not resolve GTK` 的无害提示。

## 已知限制

- **已验证环境**：Linux Mint 22.3（Ubuntu 24.04 用户空间）与 Fedora 45，均为 x86_64。
  其他发行版应能工作但未逐一验证，首次使用建议先 `diagnose` 再 `apply`。
- Fedora 上 USB 重定向不可用（见上）。
- 如果系统里装了有缺陷的 Qt4（如 rock-core），补丁只是绕开它（RPATH 优先加载内置版本），
  不会卸载系统包；其他依赖该系统 Qt4 的程序仍可能受影响。
- `libaudio2` 在新系统上通常会缺失，它是 Qt4 的 NAS 音频后端。补丁会补一份；下载失败不影响
  启动，只是多一条链接警告。
- `ld.so.conf.d` 是本补丁唯一触及系统目录的改动。它只引入 `compat/ldso/`，其中各个 SONAME
  在系统 ld.so 缓存中原本不存在，因此不改变任何现有程序的动态链接结果；`revert` 完整回滚。
- 在某些系统上会退化为 LDAP 桩库，此时客户端不具备 LDAP 功能（VDI 场景用不到）。
- 仅适配 glibc 系（Debian/Ubuntu/Fedora 等），不适用于 musl 发行版（Alpine 等）。
- `--disable-idle-logout` 的 shim 与客户端版本绑定，升级后自动失效（回到厂商默认行为）。

## 免责声明

本项目与深信服科技无关联，为社区经验整理。默认行为只调整**本机客户端运行环境**，不修改
客户端鉴权与网络接入策略。

需要特别说明的是，可选的 `--disable-idle-logout` **会让客户端的空闲自动登出不再生效**，
这属于对厂商安全设计的改动，可能不符合你所在机构的信息安全规定。请在使用前确认合规性，
并优先考虑通过 VDC 管理端调整该策略。

建议优先向厂商反馈并获取官方适配新系统的客户端；本项目适合作为过渡方案。

## 验证记录

在 Linux Mint 22.3（Ubuntu 24.04 用户空间，x86_64）上完整验证：

- 应用后点应用菜单图标，登录框正常显示，CPU 稳定在 1–2%
- `/proc/<pid>/maps` 确认加载的是 `compat/` 与 `/usr/local/Trolltech/Qt-4.8.7/lib` 下的库
- `vdi_super_agent`（systemd）与 `vdi_webui_agent`（登录自启）均在线，`listen_port` 正常
- 17 个客户端二进制 `ldd` 无 `not found`
- **完整登录并打开桌面**：`vdi_session`（保持 setuid，`/proc/<pid>/status` 显示 euid 为 0）
  以 `vdi_session.real -i <sessid> -s 0` 启动后持续运行，不再有 70ms 退出，
  X 窗口 `[<domain-uid> - <desk-name>] (Vdi Session)` 正常显示远程桌面画面
- 系统侧抽查：`curl` 仍链接 `libldap.so.2`（2.6），未被兼容库替换

在 Fedora Linux 45（Workstation，x86_64，kernel 7.2.15，Wayland 会话）上完整验证：

- 用 deb 原生方式安装（解数据 + 跑 `postinst configure`），得到 `/usr/local/sangfor/vdiclient`、
  三个 systemd 单元与桌面项；`alien` 转出的 RPM 因缺少 scriptlet 不可用
- `apply` 后 `vdi_super_agent` 转为 active 并创建 `vdi_super_agent.sock`
- `vdi_local_client` 在 Wayland/XWayland 下启动，登录框正常显示
- 17 个二进制 `ldd` 无 `not found`；仅剩 OpenSSL 1.0 无版本表的无害提示
- OpenLDAP 走桩库路径（Fedora 原生 2.4 无 `OPENLDAP_2.4_2` 版本定义）
- USB 重定向不可用，已停用 `vdi_usbipd`（原因见上文）

### 空闲登出问题的排查与验证

| 结论 | 依据 |
|---|---|
| 计时器不响应真实输入 | 1200.011s / 1200.499s 两次实测，误差极小且无重置 |
| 加 `input` 组无效 | 组权限已生效（进程 groups 含 995），仍准时登出 |
| 注入 inotify 事件无效 | 5 次触发 `/dev/input` 事件，计时器毫无变化 |
| 取值路径 | 反汇编：`NoInputTimeout → atoi (0xd2ca83) → 存入 0xa8 → >0 才进入计时循环` |
| 同客户端在 Ubuntu 18.04 不踢 | 日志显示其 `InputWatch` 无 `end running`，即服务端下发值为 0 |
| shim 命中验证 | 诊断版实测打印 `atoi("1200") from base+0xd2ca8` |
| 最终效果 | 启用 shim 后会话持续 23 分钟以上无 `InputWatch` 记录、无 logout、进程存活 |