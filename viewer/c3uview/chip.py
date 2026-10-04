"""
chip.py - what to read from the ESP32-C3, and what it means.

Every register here is safe to read while a program runs: reading it changes
nothing. Peripheral blocks (LEDC, the ADC) are only read while their clock is
on and they are out of reset, because a block that is switched off may not
answer.

Addresses and bit positions come from ESP-IDF v5.4's register headers for the
ESP32-C3 (soc/esp32c3/register/soc/*.h).
"""
from .signals import IN_SIGNALS, OUT_SIGNALS, PAD_FUNCTIONS

GPIO_COUNT = 22

# ---- register blocks: (name, address, number of 32-bit words) -------------------
GPIO_BLOCK = ("gpio", 0x60004000, 16)            # OUT at +0x04, ENABLE at +0x20, IN at +0x3C
OUT_SEL_BLOCK = ("out_sel", 0x60004554, GPIO_COUNT)  # GPIO_FUNCn_OUT_SEL_CFG, one per pin
IO_MUX_BLOCK = ("io_mux", 0x60009004, GPIO_COUNT)    # IO_MUX_GPIOn, one per pin
SYSTEM_BLOCK = ("system", 0x600C0008, 6)          # CPU_PER_CONF, -, PERIP_CLK_EN0, CLK_EN1, RST_EN0, RST_EN1
SYSCLK_BLOCK = ("sysclk", 0x600C0058, 1)          # SYSTEM_SYSCLK_CONF: the CPU's clock
IN_SEL_BLOCK = ("in_sel", 0x60004154, 128)        # GPIO_FUNCm_IN_SEL_CFG, one per input signal
LEDC_BLOCK = ("ledc", 0x60019000, 0x38)           # channels 0-5, timers 0-3, LEDC_CONF at +0xD0
ADC_BLOCK = ("adc", 0x60040000, 0x0C)             # CTRL, ..., ONETIME_SAMPLE at +0x20, 1_DATA_STATUS at +0x2C

CORE_BLOCKS = [GPIO_BLOCK, OUT_SEL_BLOCK, IO_MUX_BLOCK, SYSTEM_BLOCK, SYSCLK_BLOCK]
SLOW_BLOCKS = [IN_SEL_BLOCK]                      # changes rarely, so read less often

LEDC_CLOCK_BIT = 1 << 11                          # in SYSTEM_PERIP_CLK_EN0 / RST_EN0
ADC_CLOCK_BIT = 1 << 28
RMT_CLOCK_BIT = 1 << 9

XTAL_HZ = 40_000_000
RC_FAST_HZ = 17_500_000

# What this board does with some of its pins.
BOARD_NOTES = {
    2: "RGB LED (SK6812)",
    9: "button (low while pressed; held at power-on it starts download mode)",
    11: "flash power (VDD_SPI)",
    12: "flash", 13: "flash", 14: "flash", 15: "flash", 16: "flash", 17: "flash",
    18: "USB D-", 19: "USB D+",
    20: "RX on the ISP header (UART0)", 21: "TX on the ISP header (UART0)",
}
RESERVED = set(range(11, 20))                     # flash and USB: leave alone
ADC_PINS = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4}         # GPIO -> ADC1 channel

FRIENDLY = {
    "GPIO": "GPIO_OUT",
    "U0TXD": "UART0 TX", "U0RXD": "UART0 RX", "U1TXD": "UART1 TX", "U1RXD": "UART1 RX",
    "I2CEXT0_SCL": "I2C SCL", "I2CEXT0_SDA": "I2C SDA",
    "TWAI_TX": "TWAI (CAN) TX", "TWAI_RX": "TWAI (CAN) RX",
    "RMT_SIG_OUT0": "RMT channel 0", "RMT_SIG_OUT1": "RMT channel 1",
    "RMT_SIG_IN0": "RMT channel 2 (receive)", "RMT_SIG_IN1": "RMT channel 3 (receive)",
}
for _ch in range(6):
    FRIENDLY[f"LEDC_LS_SIG_OUT{_ch}"] = f"LEDC channel {_ch} (PWM)"


# Pads switched straight to a peripheral by IO_MUX, without the GPIO matrix.
DIRECT = {
    "MTMS": "pad JTAG TMS: the power-on default, unused (JTAG goes over USB)",
    "MTDI": "pad JTAG TDI: the power-on default, unused (JTAG goes over USB)",
    "MTCK": "pad JTAG TCK: the power-on default, unused (JTAG goes over USB)",
    "MTDO": "pad JTAG TDO: the power-on default, unused (JTAG goes over USB)",
    "U0RXD": "UART0 RX: the power-on default; the ROM's boot messages use UART0",
    "U0TXD": "UART0 TX: the power-on default; the ROM's boot messages use UART0",
}


def friendly(name):
    return FRIENDLY.get(name, name)


def plan(frame_number, system_words):
    """The blocks to read this time. system_words is the last SYSTEM block read, or None."""
    blocks = list(CORE_BLOCKS)
    if frame_number % 10 == 0:
        blocks += SLOW_BLOCKS
    if system_words:
        clk_en0, rst_en0 = system_words[2], system_words[4]
        if clk_en0 & LEDC_CLOCK_BIT and not rst_en0 & LEDC_CLOCK_BIT:
            blocks.append(LEDC_BLOCK)
        if clk_en0 & ADC_CLOCK_BIT and not rst_en0 & ADC_CLOCK_BIT:
            blocks.append(ADC_BLOCK)
    return blocks


def bit(value, n):
    return (value >> n) & 1


def apb_hz(sysclk):
    """The APB clock, which follows the CPU when it runs from the crystal, and is 80 MHz from the PLL."""
    source = (sysclk >> 10) & 3
    divider = (sysclk & 0x3FF) + 1
    if source == 1:
        return 80_000_000
    return (XTAL_HZ if source == 0 else RC_FAST_HZ) // divider


def cpu_hz(sysclk, cpu_per_conf):
    """The CPU clock. From the PLL, CPUPERIOD_SEL picks 80 MHz (0) or 160 MHz (1)."""
    if (sysclk >> 10) & 3 == 1:
        return 160_000_000 if cpu_per_conf & 3 == 1 else 80_000_000
    return apb_hz(sysclk)


def decode_ledc(ledc, channel, sysclk):
    """Frequency and duty of one LEDC channel, or None if it isn't running."""
    conf0 = ledc[channel * 5]
    duty_r = ledc[channel * 5 + 4]              # the duty the channel is using now
    if not bit(conf0, 2):                        # SIG_OUT_EN off: the output sits at its idle level
        return {"channel": channel, "running": False}
    timer = conf0 & 3
    tconf = ledc[(0xA0 + timer * 8) // 4]
    bits = tconf & 0xF
    divider = (tconf >> 4) & 0x3FFFF
    clk_sel = ledc[0xD0 // 4] & 3
    clock = {1: apb_hz(sysclk), 2: RC_FAST_HZ, 3: XTAL_HZ}.get(clk_sel, 0)
    hz = clock * 256 / (divider * (1 << bits)) if divider and clock else 0
    counts = (duty_r >> 4) & 0x7FFF
    return {"channel": channel, "running": True, "timer": timer, "bits": bits,
            "hz": round(hz, 2), "duty": round(100.0 * counts / (1 << bits), 2) if bits else 0}


def decode(regs):
    """Turn one snapshot of raw registers into a description of every pin."""
    gpio = regs["gpio"]
    out_reg, enable_reg, in_reg = gpio[1], gpio[8], gpio[15]
    sysclk = regs["sysclk"][0]
    ledc = regs.get("ledc")
    adc = regs.get("adc")
    in_sel = regs.get("in_sel") or []

    # Which peripheral inputs each pin feeds, through the GPIO matrix.
    feeds = {n: [] for n in range(GPIO_COUNT)}
    for signal, cfg in enumerate(in_sel):
        source = cfg & 0x1F
        if bit(cfg, 6) and source < GPIO_COUNT:
            feeds[source].append(friendly(IN_SIGNALS.get(signal, f"input signal {signal}")))

    adc_powered = adc is not None and ((adc[0] >> 27) & 3) == 3

    pins = []
    for n in range(GPIO_COUNT):
        mux = regs["io_mux"][n]
        out_sel = regs["out_sel"][n]
        mcu_sel = (mux >> 12) & 7
        function = PAD_FUNCTIONS.get(n, {}).get(mcu_sel, f"function {mcu_sel}")
        ie, pu, pd = bit(mux, 9), bit(mux, 8), bit(mux, 7)
        signal = out_sel & 0xFF
        oen_from_gpio = bit(out_sel, 9)
        driver_on = bit(enable_reg, n)
        level = bit(in_reg, n) if ie else None

        pin = {
            "pin": n,
            "note": BOARD_NOTES.get(n, ""),
            "reserved": n in RESERVED,
            "function": function,
            "pull": "up" if pu else "down" if pd else "none",
            "input_enabled": bool(ie),
            "level": level,
            "drive": (mux >> 10) & 3,
            "feeds": feeds[n],
        }

        # Does the pin drive? For plain GPIO, GPIO_ENABLE decides. For a peripheral's signal,
        # GPIO_ENABLE decides when OEN_SEL is set; otherwise the peripheral itself does.
        if signal == 128 or oen_from_gpio:
            driving = bool(driver_on)
        else:
            driving = True

        if n in RESERVED:
            pin["mode"] = "reserved"
            pin["summary"] = f"{BOARD_NOTES[n]}: in use by the board, leave it alone"
        elif function != "GPIO":
            pin["mode"] = "peripheral"
            pin["summary"] = DIRECT.get(function, f"IO_MUX function {function}")
        elif driving:
            source = OUT_SIGNALS.get(signal, f"signal {signal}")
            pin["mode"] = "output"
            pin["driven_by"] = friendly(source)
            if signal == 128:
                pin["out_level"] = bit(out_reg, n)
                pin["summary"] = f"output, set to {bit(out_reg, n)}"
            else:
                pin["summary"] = f"output, driven by {friendly(source)}"
            if 45 <= signal <= 50 and ledc:
                pwm = decode_ledc(ledc, signal - 45, sysclk)
                pin["pwm"] = pwm
                if pwm["running"]:
                    pin["summary"] = f"PWM, LEDC channel {pwm['channel']}: {pwm['hz']:g} Hz, {pwm['duty']:g}%"
                else:
                    pin["summary"] = f"PWM, LEDC channel {pwm['channel']}, stopped (low)"
        elif ie:
            pin["mode"] = "input"
            pin["summary"] = f"input, pull-{pin['pull']}" if pin["pull"] != "none" else "input, no pull"
        elif n in ADC_PINS and adc_powered:
            pin["mode"] = "analog"
            pin["summary"] = "analog input (ADC1 channel %d)" % ADC_PINS[n]
        else:
            pin["mode"] = "off"
            pin["summary"] = "not connected to anything inside the chip"
        if pin["feeds"]:
            pin["summary"] += "; feeds " + ", ".join(pin["feeds"])
        pins.append(pin)

    chip = {"cpu_hz": cpu_hz(sysclk, regs["system"][0]), "apb_hz": apb_hz(sysclk),
            "ledc_on": ledc is not None, "adc_on": adc is not None}
    if adc is not None:
        onetime = adc[0x20 // 4]
        chip["adc_last"] = adc[0x2C // 4] & 0xFFF
        chip["adc_powered"] = adc_powered
        chip["adc_atten"] = (onetime >> 23) & 3
    return {"pins": pins, "chip": chip}
