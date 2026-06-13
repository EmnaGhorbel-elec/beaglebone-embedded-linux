# Embedded Linux — PWM Measurement System
### BeagleBone Black

A complete embedded Linux system that measures PWM timing precision using a Linux kernel module,
a multi-threaded userspace controller, and an automated error characterization tool.

**Authors: Emna GHORBEL & Ichrak AYADI — ENIT 2025/2026**

---

## How it works

```
Potentiometer (P9_38)
        │
        ▼ ADC value 0–4095
led_pwm_control           ←── Scenario A: interactive
        │
        ▼ PWM period 2ms–50ms
    P9_14 (ehrpwm1a)
        │
        └──[wire]──→ P9_11 (IRQ input)
                          │
                          ▼ rising edge → hardware interrupt
                   kernel_module (speed_measure.ko)
                          │
                          ├──→ DT = time between pulses (ns)
                          ├──→ /proc/SpeedM_dev  ←── read by both apps
                          └──→ P8_10 LED toggles on each pulse

pwm_error_measure         ←── Scenario B: automatic sweep + CSV + graph
```

---

## Hardware setup

| Pin | Role |
|-----|------|
| P9_14 (ehrpwm1a) | PWM output → wire to P9_11 |
| P9_11 (GPIO0_30) | IRQ input (kernel module) |
| P8_10 (GPIO2_4) | LED toggle — kernel module output |
| P9_38 (AIN3) | Potentiometer wiper |
| P9_32 (VDD_ADC) | Potentiometer 1.8V supply |
| P9_34 (AGND) | Potentiometer ground |

> The single required wire: **P9_14 → P9_11**

> ADC max input is **1.8V**. Never use 3.3V — it damages the processor.

> On this board `pwmchip0` = **P9_14** (ehrpwm1a).
> Verify with: `sudo cat /sys/kernel/debug/gpio | grep pwm`

---

## 1. kernel_module

### What it does

A Linux platform driver that attaches to the hardware via Device Tree.
It intercepts every rising edge on P9_11 via a hardware interrupt,
measures the time between consecutive pulses in nanoseconds,
and exposes the result through `/proc/SpeedM_dev` readable by any userspace program.
It also toggles the LED on P8_10 on each interrupt as visual confirmation.

### Files

```
kernel_module/
├── speed_measure.c   ← the kernel module
├── SpeedMmeas.dts    ← Device Tree overlay
└── Makefile
```

### Device Tree overlay

```dts
/dts-v1/;
/plugin/;
&{/} {
    SpeedM_device {
        compatible = "MP_emb,sm-pulse";
        inputcapture-gpios    = <&gpio0 30 0>;  /* P9_11 */
        inputcaptureLED-gpios = <&gpio2 4 0>;   /* P8_10 */
        status = "okay";
    };
};
```

The `compatible` string links the `.dts` to the driver.
When the module loads, the kernel matches this string and calls `SpeedM_probe()` automatically.

### IRQ handler

```c
static irqreturn_t SpeedM_handler(int irq, void *dev_id) {
    u64 tme = ktime_get_ns();
    u64 dt  = tme - T1;

    if (dt < 1000000) return IRQ_HANDLED;  // noise filter < 1ms

    DT = dt;   // nanoseconds between two rising edges
    T1 = tme;

    gpiod_set_value(desc_out, !gpiod_get_value(desc_out)); // toggle P8_10
    return IRQ_HANDLED;
}
```

### Build and deploy

```bash
# Step 1 — compile the overlay (on BBB)
dtc -I dts -O dtb -o SpeedMmeas.dtbo SpeedMmeas.dts

# Step 2 — install overlay
sudo cp SpeedMmeas.dtbo /boot/dtbs/$(uname -r)/
sudo cp SpeedMmeas.dtbo /boot/dtbs/$(uname -r)/overlays/

# Step 3 — enable at boot
sudo nano /boot/uEnv.txt
# add: uboot_overlay_addr6=SpeedMmeas.dtbo
sudo reboot

# Step 4 — verify overlay loaded
ls /proc/device-tree/SpeedM_device   # must exist

# Step 5 — install kernel headers and compile (on BBB)
sudo apt install linux-headers-$(uname -r)
make

# Step 6 — load module
sudo insmod speed_measure.ko

# Step 7 — verify
dmesg | tail -n 5
# → SpeedM_meas: probe launched.
# → SpeedM_meas: /proc/SpeedM_dev created.

gpioinfo | grep -E "(P9_11|P8_10)"
# → "used"  (shows "unused" before insmod)

cat /proc/SpeedM_dev
# → 29940542   (DT in nanoseconds)

# Unload when done
sudo rmmod speed_measure
```

---

## 2. led_pwm_control — Scenario A (interactive)

### What it does

A multi-threaded application that maps a potentiometer to a PWM period in real time.
Three POSIX threads run in cascade driven by a 100ms timer:
Thread 0 ticks, Thread 1 reads the ADC, Thread 2 sets the PWM period and reads
back the measurement from the kernel module to display live frequency and RPM.

### Thread cascade

```
TIMER (100ms auto-repeat)
    │ signal cv0
    ▼
Thread 0 — prints [T0] tick
    │ signal cv1
    ▼
Thread 1 — reads ADC (0–4095)
    │ signal cv2
    ▼
Thread 2 — maps ADC → period (2ms–50ms)
           writes period to sysfs PWM
           reads DT from /proc/SpeedM_dev
           computes frequency + RPM
           prints result
```

### Period mapping

```c
period = PERIOD_MIN +
    (uint32_t)((adc_val * (uint64_t)(PERIOD_MAX - PERIOD_MIN)) >> 12);
// PERIOD_MIN = 2 000 000 ns  (2ms  = 500Hz)
// PERIOD_MAX = 50 000 000 ns (50ms =  20Hz)

duty = period / 2;  // 50% square wave → clean rising edges for IRQ
```

### RPM calculation

```c
DT   = read_proc("/proc/SpeedM_dev");  // nanoseconds
freq = 1e9 / (double)DT;              // Hz
rpm  = freq * 60.0;                   // RPM (1 pulse per revolution)
```

### Build and run

```bash
# Cross-compile on host
make
scp build/led_pwm_control debian@<BBB_IP>:~

# Run on BBB (kernel module must be loaded first)
./led_pwm_control
# Turn potentiometer to change speed — press 's' to stop
```

### Terminal output (real measured values)

```
[T0] tick -> [ADC] 512  -> [PWM]  8.5 ms | [DT]  8501234 ns | [FREQ] 117.6 Hz | [RPM]  7058
[T0] tick -> [ADC] 2385 -> [PWM] 29.9 ms | [DT] 29940542 ns | [FREQ]  33.4 Hz | [RPM]  2004
[T0] tick -> [ADC] 4095 -> [PWM] 50.0 ms | [DT] 50001000 ns | [FREQ]  20.0 Hz | [RPM]  1200
```

DT from kernel matches PWM period to within ~60 microseconds. ✅

---

## 3. pwm_error_measure — Scenario B (automatic)

### What it does

Automatically sweeps PWM periods from 10ms to 100ms in 5ms steps,
compares what was sent to what the kernel module actually measured,
and quantifies the hardware error of the BBB PWM controller.
Results are saved to CSV and optionally plotted with gnuplot.

### Measurement loop

```
T = 10ms
loop:
    T += 5ms  (resets to 10ms after 100ms)
    write T to /sys/.../period
    wait 100ms stabilization
    read DT from /proc/SpeedM_dev
    error_us  = T_sent_us − T_read_us
    error_pct = (error_us / T_sent_us) × 100
    store to CSV
    sleep 1s
until 'q' or 1000 samples
```

### Build and run

```bash
# Compile on BBB
gcc pwm_main.c -o pwm_test -lm

./pwm_test          # CSV only
./pwm_test --save   # CSV + PNG graph (requires gnuplot)
```

### Output files

| File | Content |
|------|---------|
| `pwm_error_log.csv` | step, T_sent_us, T_read_us, error_us, error_pct |
| `pwm_error_curve.png` | 3 graphs: T_sent vs T_read · absolute error · relative error |

### Terminal output

```
Étape  T_envoyé(µs)    T_lu(µs)     Erreur(µs)   Erreur(%)
0      15000           14997        +3           +0.02%
1      20000           19994        +6           +0.03%
2      25000           24991        +9           +0.04%
```

**Result: PWM precision validated at under 0.05% across the full range.**

---

## Run order

```bash
# 1. Load kernel module first — always
sudo insmod kernel_module/speed_measure.ko
cat /proc/SpeedM_dev   # verify it works

# 2a. Scenario A — interactive control
./led_pwm_control

# 2b. Scenario B — automatic characterization
./pwm_error_measure/pwm_test --save
```

---

## Diagnostic commands

```bash
# Is the kernel module active?
dmesg | tail -n 5

# Are the GPIOs claimed by the module?
gpioinfo | grep -E "(P9_11|P8_10)"

# Is the IRQ being triggered?
watch -n 0.2 'cat /proc/interrupts | grep SpeedM'

# Is the PWM period changing with the pot?
watch -n 0.2 cat /sys/class/pwm/pwmchip0/pwm0/period

# Read DT directly from kernel
cat /proc/SpeedM_dev
```

---

## Stack

C · Linux Kernel Module · Device Tree · GPIO / PWM / ADC / IRQ ·
POSIX Threads · Mutex / Condition Variables · POSIX Timer ·
BeagleBone Black · ARM Cortex-A8 · Cross-compilation · sysfs · /proc · gnuplot
