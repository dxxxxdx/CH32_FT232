#!/usr/bin/env python3
"""选择板型或自定义引脚，只保存 userconfig.cmake，不启动编译。"""

from dataclasses import dataclass
from pathlib import Path
import re
import tempfile
import tkinter as tk
from tkinter import messagebox, ttk


PROJECT_DIR = Path(__file__).resolve().parent.parent
CONFIG_FILE = PROJECT_DIR / "userconfig.cmake"
SIGNALS = ("TCK", "TMS", "TDI", "TDO")
PINS = tuple(f"PA{index}" for index in range(8))
PRESET_NAMES = ("20PINOUT", "22PINOUT")
UART_LABELS = {
    "PA23": "PA2 / TX、PA3 / RX",
    "PB67": "PB6 / TX、PB7 / RX",
    "DISABLED": "禁用 UART",
}
BASE_KEYS = {"USER_CONFIG_VERSION", "USER_BOARD_MODE"}
CUSTOM_KEYS = {*(f"USER_JTAG_{signal}" for signal in SIGNALS), "USER_UART_MODE"}


@dataclass(frozen=True)
class PinConfig:
    mode: str
    pins: tuple[str, ...]
    uart: str


def validate(config: PinConfig) -> None:
    if config.mode not in (*PRESET_NAMES, "CUSTOM"):
        raise ValueError("板型必须是 20PINOUT、22PINOUT 或 CUSTOM。")
    if len(config.pins) != 4 or any(pin not in PINS for pin in config.pins):
        raise ValueError("四个 JTAG 信号都必须选择 PA0～PA7。")
    owners: dict[str, str] = {}
    for signal, pin in zip(SIGNALS, config.pins):
        if pin in owners:
            raise ValueError(f"{pin} 同时分配给 {owners[pin]} 和 {signal}，请选择不同引脚。")
        owners[pin] = signal
    if config.uart not in UART_LABELS:
        raise ValueError("UART 必须选择 PA2/PA3、PB6/PB7 或禁用。")
    if config.uart == "PA23":
        conflicts = [f"{owners[pin]}={pin}" for pin in ("PA2", "PA3") if pin in owners]
        if conflicts:
            raise ValueError("UART 占用 PA2/PA3，与 JTAG 冲突：" + "、".join(conflicts))


def load_presets(board_dir: Path) -> dict[str, PinConfig]:
    """直接读取板型头；GUI 不维护另一套预设引脚数值。"""
    presets = {}
    for name in PRESET_NAMES:
        source = (board_dir / f"{name.lower()}.h").read_text(encoding="utf-8")
        for port in ("OUTPUT", "TDO"):
            if not re.search(rf"^#define\s+BOARD_JTAG_{port}_PORT\s+GPIOA\s*$", source, re.M):
                raise ValueError(f"{name} 的 JTAG 端口不在当前界面支持的 GPIOA 范围内。")
        pins = []
        for signal in SIGNALS:
            match = re.search(rf"^#define\s+BOARD_JTAG_{signal}_PIN\s+\(([0-7])U\)", source, re.M)
            if not match:
                raise ValueError(f"{name} 缺少 PA0～PA7 范围内的 {signal} 定义。")
            pins.append(f"PA{match[1]}")
        route = re.search(r'^#include\s+"Ch32V203Usart[12](Pa23|Pb67)\.h"', source, re.M)
        if not route:
            raise ValueError(f"{name} 缺少支持的 UART 路由定义。")
        config = PinConfig(name, tuple(pins), route[1].upper())
        validate(config)
        presets[name] = config
    return presets


def read_config(path: Path, presets: dict[str, PinConfig]) -> PinConfig:
    values = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r'set\(([A-Z_]+)[ \t]+"([A-Za-z0-9_]+)"\)', line)
        if not match:
            raise ValueError(f"配置第 {number} 行格式错误。")
        key, value = match.groups()
        if key not in BASE_KEYS | CUSTOM_KEYS or key in values:
            raise ValueError(f"配置键未知或重复：{key}")
        values[key] = value
    if values.get("USER_CONFIG_VERSION") != "1":
        raise ValueError("配置缺少 USER_CONFIG_VERSION，或版本不是 1。")
    mode = values.get("USER_BOARD_MODE")
    if mode in presets:
        if set(values) != BASE_KEYS:
            raise ValueError("预设不能覆盖单独引脚，需要修改时请选择自定义。")
        return presets[mode]
    if mode != "CUSTOM":
        raise ValueError("配置缺少有效的 USER_BOARD_MODE。")
    missing = (BASE_KEYS | CUSTOM_KEYS) - values.keys()
    if missing:
        raise ValueError("配置缺少：" + "、".join(sorted(missing)))
    config = PinConfig(mode, tuple(values[f"USER_JTAG_{signal}"] for signal in SIGNALS), values["USER_UART_MODE"])
    validate(config)
    return config


def save_config(path: Path, config: PinConfig) -> None:
    validate(config)
    values = {"USER_CONFIG_VERSION": "1", "USER_BOARD_MODE": config.mode}
    if config.mode == "CUSTOM":
        values.update({f"USER_JTAG_{signal}": pin for signal, pin in zip(SIGNALS, config.pins)})
        values["USER_UART_MODE"] = config.uart
    content = "# 由 PinSelectGUI 保存；时间戳由 CMake 在编译时独立生成。\n"
    content += "\n".join(f'set({key} "{value}")' for key, value in values.items()) + "\n"
    # 同目录原子替换，CMake 不会读到保存一半的配置。
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=path.parent, prefix=".userconfig-", suffix=".tmp",
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(content)
        temporary.replace(path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


class PinSelectApp:
    def __init__(self, root: tk.Tk, config_file: Path = CONFIG_FILE,
                 board_dir: Path = PROJECT_DIR / "boardtype"):
        self.root = root
        self.config_file = config_file
        self.presets = load_presets(board_dir)
        initial = self.presets["20PINOUT"]
        load_error = None
        if config_file.exists():
            try:
                initial = read_config(config_file, self.presets)
            except (OSError, ValueError) as error:
                load_error = str(error)
        self.mode = tk.StringVar(root, initial.mode)
        self.pins = {signal: tk.StringVar(root, pin) for signal, pin in zip(SIGNALS, initial.pins)}
        self.uart = tk.StringVar(root, UART_LABELS[initial.uart])
        self.summary = tk.StringVar(root)
        self.status = tk.StringVar(root)
        self._build_widgets()
        self._select_mode()
        if load_error:
            self.status.set("读取配置失败；选择配置后可重新保存。")
            messagebox.showerror("无法读取已有配置", load_error, parent=root)
        elif config_file.exists():
            self.status.set("已读取上次保存的配置。")
        else:
            self.status.set("首次使用：默认 20PINOUT，点击保存后生效。")

    def _build_widgets(self) -> None:
        self.root.title("CH32 引脚配置")
        self.root.minsize(640, 420)
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        panel = ttk.Frame(self.root, padding=20)
        panel.grid(sticky="nsew")
        panel.columnconfigure(0, weight=1)
        ttk.Label(panel, text="选择板型", font=("", 16, "bold")).grid(sticky="w")
        ttk.Label(panel, text="使用现有预设，或展开自定义引脚。保存后再通过 CMake 编译。").grid(sticky="w", pady=(6, 16))
        choices = ttk.Frame(panel)
        choices.grid(sticky="w")
        for column, (label, value) in enumerate((("20PINOUT", "20PINOUT"), ("22PINOUT", "22PINOUT"), ("自定义", "CUSTOM"))):
            ttk.Radiobutton(choices, text=label, value=value, variable=self.mode,
                            command=self._select_mode).grid(row=0, column=column, padx=(0, 24))
        self.custom = ttk.LabelFrame(panel, text="自定义引脚", padding=12)
        self.custom.grid(row=3, column=0, sticky="ew", pady=(16, 0))
        self.custom.columnconfigure(1, weight=1)
        for row, signal in enumerate(SIGNALS):
            ttk.Label(self.custom, text=signal).grid(row=row, column=0, sticky="w", padx=(0, 16), pady=3)
            box = ttk.Combobox(self.custom, values=PINS, textvariable=self.pins[signal], state="readonly", width=12)
            box.grid(row=row, column=1, sticky="w", pady=3)
            box.bind("<<ComboboxSelected>>", self._refresh)
        ttk.Label(self.custom, text="UART").grid(row=4, column=0, sticky="w", pady=3)
        uart_box = ttk.Combobox(self.custom, values=tuple(UART_LABELS.values()),
                               textvariable=self.uart, state="readonly", width=30)
        uart_box.grid(row=4, column=1, sticky="w", pady=3)
        uart_box.bind("<<ComboboxSelected>>", self._refresh)
        preview = ttk.LabelFrame(panel, text="将保存的配置", padding=12)
        preview.grid(row=4, column=0, sticky="ew", pady=16)
        ttk.Label(preview, textvariable=self.summary, justify="left").grid(sticky="w")
        ttk.Label(panel, textvariable=self.status, wraplength=600).grid(row=5, column=0, sticky="w")
        ttk.Label(panel, text=f"配置文件：{self.config_file}", wraplength=600).grid(row=6, column=0, sticky="w", pady=(8, 16))
        buttons = ttk.Frame(panel)
        buttons.grid(row=7, column=0, sticky="e")
        self.save_button = ttk.Button(buttons, text="保存配置", command=self._save)
        self.save_button.grid(row=0, column=0, padx=(0, 8))
        ttk.Button(buttons, text="关闭", command=self.root.destroy).grid(row=0, column=1)

    def _select_mode(self) -> None:
        if self.mode.get() in self.presets:
            preset = self.presets[self.mode.get()]
            for signal, pin in zip(SIGNALS, preset.pins):
                self.pins[signal].set(pin)
            self.uart.set(UART_LABELS[preset.uart])
            self.custom.grid_remove()
        else:
            self.custom.grid()
        self._refresh()

    def _current_config(self) -> PinConfig:
        if self.mode.get() in self.presets:
            return self.presets[self.mode.get()]
        uart = next((key for key, label in UART_LABELS.items() if label == self.uart.get()), "")
        return PinConfig(self.mode.get(), tuple(self.pins[signal].get() for signal in SIGNALS), uart)

    def _refresh(self, _event=None) -> None:
        config = self._current_config()
        self.summary.set("    ".join(f"{signal}: {pin}" for signal, pin in zip(SIGNALS, config.pins))
                         + f"\nUART: {UART_LABELS.get(config.uart, '未选择')}"
                         + "\nUSB: PA11 / PA12；UART 线编码: 115200 8N1")
        try:
            validate(config)
        except ValueError as error:
            self.status.set(str(error))
            self.save_button.state(["disabled"])
        else:
            self.status.set("配置有效，点击保存后生效。")
            self.save_button.state(["!disabled"])

    def _save(self) -> None:
        try:
            save_config(self.config_file, self._current_config())
        except (OSError, ValueError) as error:
            messagebox.showerror("保存失败", str(error), parent=self.root)
            return
        self.status.set("配置已保存。可以关闭窗口，使用 CMake 编译。")


def main() -> None:
    root = tk.Tk()
    try:
        PinSelectApp(root)
    except (OSError, ValueError) as error:
        root.withdraw()
        messagebox.showerror("无法加载板型", str(error), parent=root)
        root.destroy()
        return
    root.mainloop()


if __name__ == "__main__":
    main()
