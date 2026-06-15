# smcfan — 固定 Mac 风扇转速的小服务

把 macOS 的 SMC 风扇转速锁定在一个固定 RPM，并开机自启、每 60 秒自动重申。
专为**自动调速坏掉的机器**准备（典型症状：风扇要么一直拉满狂转，要么完全
不转）。在这种机器上，让风扇稳定在一个固定转速比坏掉的 auto 模式好得多。

支持 Apple Silicon（`flt` 风扇键）和 Intel（`fpe2` / 老款 `FS!` 位掩码）。

---

## ⚠️ 先读这段（重要）

这个工具会**关闭 SMC 的自动温控、把风扇钉死在固定转速**。

- 如果你的机器**自动调速是正常的**，其实不需要装这个；装了反而有风险——
  高负载时风扇不会自动加速，**设太低可能导致过热降频甚至关机**。
- 只在「auto 模式坏掉」的机器上用。先跑 `smcfan status` 看一眼当前状况再决定。
- 设定值会自动夹在风扇的 min/max 之间，但 **min 在重载下仍可能偏低**，请自己
  根据机型和负载选一个安全转速（很多 Apple Silicon 机型 2900~3500 比较稳妥）。
- 想反悔随时可以 `sudo smcfan auto` 恢复自动，或 `sudo ./uninstall.sh` 卸载。

出问题概不负责，自行评估 😄

---

## 安装

```sh
# 解压后进入目录
cd smcfan-fancontrol

# 默认 2900 rpm
sudo ./install.sh

# 或指定转速（1000~7000）
sudo ./install.sh 3200
```

安装脚本会自动：

1. **有 Xcode 命令行工具**就从源码 `smcfan.c` 现场编译；**没有**就用随包附带的
   预编译二进制 `smcfan-arm64`（仅 Apple Silicon）。Intel 机器若无编译器，
   先装工具：`xcode-select --install`。
2. 装到 `/usr/local/bin/smcfan`。
3. 先验证能读到 SMC 和风扇（读不到就中止，不会装服务）。
4. 装 launchd 服务 `/Library/LaunchDaemons/com.local.smcfan.plist`，开机自启 +
   每 60 秒重申（睡眠唤醒、SMC 复位后也会自动恢复）。
5. 日志写到 `/var/log/smcfan.log`。

如果检测到 **Macs Fan Control** 在跑，脚本会提示——它会和本服务抢风扇控制权，
要让本服务生效得退出它并移除它的特权 helper。

---

## 用法

```sh
smcfan status         # 查看风扇数、实际/目标转速、模式（不需 root）
sudo smcfan set 2900  # 强制所有风扇到 2900 rpm（自动夹到 min/max）
sudo smcfan auto      # 恢复 SMC 自动模式（坏掉的机器上 = 满转）
sudo smcfan id        # 读风扇描述符 F<n>ID（Intel 有，Apple Silicon 多半没有）
sudo smcfan keys F    # dump 所有 F 开头的 SMC 键（诊断用）
```

`status` 输出示例：

```
fans: 2
fan0: actual=2902 target=2900 min=2317 max=6800 mode=forced
fan1: actual=2895 target=2900 min=2502 max=6800 mode=forced
```

`mode=forced` 表示已被锁定，`auto` 表示交还给 SMC。

---

## 改转速 / 卸载

```sh
sudo smcfan set 3500            # 临时改一次（重启后会回到 plist 里的默认值）
```

想**永久改默认转速**，重新跑安装即可：`sudo ./install.sh 3500`。

```sh
sudo ./uninstall.sh            # 停服务、删文件、恢复风扇 auto 模式
```

注意：卸载后如果这台机器的 SMC auto 本来就是坏的，风扇会回到满转——那是 SMC
的默认行为，不是卸载脚本的问题。

---

## 文件清单

| 文件 | 说明 |
|---|---|
| `smcfan.c` | 源码，通过 IOKit 直接读写 AppleSMC |
| `com.local.smcfan.plist` | launchd 服务模板（`__RPM__` 安装时被替换） |
| `install.sh` | 编译/安装/加载，一步到位 |
| `uninstall.sh` | 停服务、卸载、恢复 auto |
| `smcfan-arm64` | 预编译二进制（Apple Silicon，无编译器时用） |
| `README.md` | 本文档 |

---

## 常见问题

**Q: 设了转速但读回来不对 / 一会儿又变了？**
多半是有别的风扇控制软件（Macs Fan Control 之类）在抢。退出它、移除它的
LaunchDaemon helper 后再试。`ps aux | grep -i fan` 能看到谁在跑。

**Q: `smcfan auto` 之后风扇狂转？**
说明这台机器的 SMC 自动温控就是坏的——这正是你需要这个服务的原因。重新
`sudo smcfan set <rpm>` 锁回去即可。

**Q: Intel Mac 能用吗？**
能。源码同时支持 Intel 的 `fpe2` 风扇键和老款 `FS!` 位掩码模式。但随包的预编译
二进制只有 arm64，Intel 机器需要本机编译（先 `xcode-select --install`）。

**Q: 安全吗？会不会烧机器？**
SMC 的硬件级过温保护仍然在；但本服务关掉了自动加速曲线，所以**重载场景请自己
选够高的转速**。拿不准就别设太低，或者干脆别在温控正常的机器上用。
