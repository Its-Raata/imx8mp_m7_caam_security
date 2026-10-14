# Compile the M7 firmware from scratch

**Author:** Raata \<its.raata@gmail.com\>  
**What this document is:** a host-PC recipe to clone this repository and
produce `imx8mp_m7_caam_security.bin`.  
**What it is not:** a Yocto / ATF / U-Boot / Linux kernel build. Those live
in NXP’s BSP. You only need them to *boot* the binary, not to *compile* it.

Recommended versions (what this tree is known to build with):

| Item                | Version |
|---------------------|---------|
| MCUXpresso IDE      | 25.6.136 |
| Arm GNU Toolchain   | 14.2.Rel1 (`arm-none-eabi-gcc` 14.2.1) |
| MCUXpresso SDK      | MIMX8ML8 / evkmimx8mp, `core/` layout (`mcuxsdk`) |
| Host                | Linux (x86_64 or AArch64), CMake 3.20+, Ninja, Git |

---

## 0. Two different programs, two different compilers

The repository contains two things. Do not mix their toolchains.

| Piece                                | Who runs it              | CPU / ABI                       | Compiler you need                                |
|--------------------------------------|--------------------------|---------------------------------|--------------------------------------------------|
| M7 firmware (`armgcc/`)              | Cortex-M7 on the EVK     | Armv7-M Thumb, EABI, hard-float | `arm-none-eabi-gcc` (bare-metal)                 |
| Linux client (`crypto_rpmsg/linux/`) | Cortex-A53 Linux         | AArch64 Linux                   | ordinary `gcc` on the board (or a matching sysroot) |

The rest of this document is about the **M7 firmware**. The client is one
`gcc` line at the end.

The i.MX 8M Plus SoC has **two** Arm families on one chip:

| Core        | Role in this project              | Instruction set | This firmware’s `-mcpu` |
|-------------|-----------------------------------|-----------------|-------------------------|
| Cortex-A53  | Linux                             | AArch64         | **not used**            |
| Cortex-M7   | FreeRTOS + RPMsg + CAAM           | Armv7-M / Thumb | `-mcpu=cortex-m7`       |

So: **do not** install `aarch64-none-elf-gcc` or `aarch64-linux-gnu-gcc` to
build the firmware. Those target the A53. The M7 needs **`arm-none-eabi`**.

NXP’s SDK package is often named `mcuxsdk-aarch64`. That name is the
**package family** for i.MX 8M (the SoC is AArch64 + M7). It is **not** the
compiler triple.

---

## 1. Host computer

Install the host tools (Debian / Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y git cmake ninja-build python3 python3-pip wget tar
```

Check:

```bash
uname -m
git --version
cmake --version   # 3.20+ is comfortable; 4.x is fine
ninja --version
```

You can build on an AArch64 Linux PC as well, as long as `arm-none-eabi-gcc`
is a **cross** compiler for Cortex-M, not a native AArch64 compiler.

---

## 2. Download this project’s source

```bash
git clone https://github.com/Its-Raata/imx8mp_m7_caam_security.git
cd imx8mp_m7_caam_security
```

Pick a tag if you want a frozen milestone instead of `main`:

```bash
git checkout step-02    # CAAM GCM / HMAC + black blobs
```

You now have C files, `armgcc/CMakeLists.txt`, and linker scripts. You still
**do not** have NXP’s device headers, startup, FreeRTOS, or RPMsg-Lite.
Those come from the MCUXpresso SDK in the next steps.

---

## 3. The compiler (what, which version, where from)

### 3.1 Identity

| Item           | Value |
|----------------|-------|
| Program        | GNU Arm Embedded (`arm-none-eabi-gcc`) |
| Target triple  | `arm-none-eabi` (bare metal, no Linux, no glibc) |
| CPU            | Cortex-M7 (`-mcpu=cortex-m7 -mthumb`) |
| FPU            | FPv5 single-precision (`-mfloat-abi=hard -mfpu=fpv5-sp-d16`) |
| C library      | newlib + nano (`--specs=nano.specs --specs=nosys.specs`) |
| Version        | **Arm GNU Toolchain 14.2.Rel1** (`14.2.1 20241119`, build `arm-14.52`) |
| Typical source | bundled inside **MCUXpresso IDE 25.6.136**, or Arm’s standalone tarball |

NXP’s CMake toolchain file (`armgcc.cmake`) **requires** `ARMGCC_DIR`.
`ARMGCC_DIR` is the directory that **contains** `bin/arm-none-eabi-gcc`,
not the `bin` directory itself. If it is unset, configure dies with:

```text
***Please set ARMGCC_DIR in envionment variables***
```

(That typo is NXP’s, not yours.)

Confirm:

```bash
export ARMGCC_DIR=/path/to/arm-gnu-toolchain
$ARMGCC_DIR/bin/arm-none-eabi-gcc --version
```

A matching version prints:

```text
arm-none-eabi-gcc (Arm GNU Toolchain 14.2.Rel1 (Build arm-14.52)) 14.2.1 20241119
```

### 3.2 Option A — MCUXpresso IDE

1. Download **MCUXpresso IDE 25.6.136** for Linux from NXP (`.deb` or `.bin`
   installer).
2. Install it. NXP’s usual prefix is
   `/usr/local/mcuxpressoide-25.6.136/`.
3. The GCC lives at `…/ide/tools`. You do **not** have to open the GUI to
   compile this repo. You only need that `tools` directory.

```bash
export ARMGCC_DIR=/usr/local/mcuxpressoide-25.6.136/ide/tools
export PATH="$ARMGCC_DIR/bin:$PATH"
```

If you installed a different version, change the directory name to match.

### 3.3 Option B — standalone Arm GNU Toolchain

1. Go to Arm’s GNU toolchain downloads:
   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
2. Get **Arm GNU Toolchain 14.2.Rel1** for:
   - **Host OS:** Linux
   - **Host architecture:** x86_64 (or AArch64 if that is your PC)
   - **Target:** **AArch32 bare-metal** → archive name contains `arm-none-eabi`
3. Unpack, for example:

```bash
sudo mkdir -p /opt
sudo tar -C /opt -xf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
export ARMGCC_DIR=/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
export PATH="$ARMGCC_DIR/bin:$PATH"
arm-none-eabi-gcc --version
```

Do **not** pick:

| Archive that looks tempting | Why it is wrong here |
|-----------------------------|----------------------|
| `aarch64-none-elf`          | A53 bare metal, not M7 |
| `aarch64-none-linux-gnu`    | Linux userland for A53 |
| `arm-none-linux-gnueabihf`  | Linux userland for A-profile, not M7 |

A slightly newer 14.x or 13.x `arm-none-eabi` will usually work. Stay on
14.2.Rel1 if you want the same toolchain this project documents.

---

## 4. MCUXpresso SDK (NXP headers and middleware)

CMake needs:

```text
$SdkRootDirPath/core/tools/cmake_toolchain_files/armgcc.cmake
```

That file, the MIMX8ML8 device pack, FreeRTOS, and RPMsg-Lite all live in
**MCUXpresso SDK**. Device name in the flags is `CPU_MIMX8ML8DVNLZ_cm7`
(i.MX 8M Plus, Cortex-M7).

### 4.1 What “good” looks like

After install you must have **all** of these:

```text
$SdkRootDirPath/core/tools/cmake_toolchain_files/armgcc.cmake
$SdkRootDirPath/core/devices/MIMX8ML8/
```

`SdkRootDirPath` is wherever **you** unpacked or cloned the SDK. Do not
commit that path.

### 4.2 Option A — MCUXpresso IDE

MCUXpresso IDE 25.x can install the west-based SDK into the workspace as
`mcuxsdk-aarch64`.

In the IDE: install the SDK for **MIMX8ML8** / **evkmimx8mp**, host Linux,
toolchain GCC. Point `SdkRootDirPath` at the folder that contains `core/`.

### 4.3 Option B — SDK Builder zip

1. https://mcuxpresso.nxp.com/en/select (NXP account required)
2. Processor / board: **MIMX8ML8** / **EVK-MIMX8MP**
3. Host OS: Linux
4. Toolchain: GCC Arm Embedded
5. Include FreeRTOS and multicore / RPMsg-Lite
6. Download the archive and unpack it. The unpack root **is**
   `SdkRootDirPath` if `core/tools/cmake_toolchain_files/armgcc.cmake`
   exists there. Older zips used a flatter `devices/MIMX8ML8/` layout;
   this CMake tree expects the **`core/`** layout (`mcuxsdk`).

### 4.4 Option C — west (git)

```bash
python3 -m pip install --user west
mkdir -p ~/mcux
cd ~/mcux
west init -m https://github.com/nxp-mcuxpresso/mcuxsdk-manifests west-workspace
cd west-workspace
west update
```

Then set `SdkRootDirPath` to the directory that contains `core/` (often the
workspace root or a `mcuxsdk` subdirectory). Look for
`core/tools/cmake_toolchain_files/armgcc.cmake`.

Use a revision close to MCUXpresso **25.6.136**. Mixing a very old SDK 2.14
tree with this CMake project will fail on include paths.

---

## 5. Environment (do this in every new shell)

```bash
# Compiler (Option A or B from section 3) — directory that contains bin/
export ARMGCC_DIR=/path/to/arm-gnu-toolchain

# SDK — directory that contains core/
export SdkRootDirPath=/path/to/mcuxsdk-aarch64

export PATH="$ARMGCC_DIR/bin:$PATH"
```

Sanity checks:

```bash
test -x "$ARMGCC_DIR/bin/arm-none-eabi-gcc" && echo "compiler OK"
test -f "$SdkRootDirPath/core/tools/cmake_toolchain_files/armgcc.cmake" && echo "SDK OK"
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -E - </dev/null
```

The last command must exit 0.

---

## 6. Configure and build the firmware

`armgcc/CMakePresets.json` includes `armgcc/mcux_include.json`. That file is
IDE-generated and often holds **absolute paths from whoever last used the
IDE**. Do not rely on `cmake --preset debug` unless you have edited those
paths for your own tree (and keep personal paths out of git).

Call CMake with the toolchain file yourself:

```bash
cd /path/to/imx8mp_m7_caam_security

cmake -S armgcc -B armgcc/debug \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=debug \
  -DCMAKE_TOOLCHAIN_FILE="$SdkRootDirPath/core/tools/cmake_toolchain_files/armgcc.cmake" \
  -DSdkRootDirPath="$SdkRootDirPath" \
  -DLIBRARY_TYPE=NEWLIB \
  -DLANGUAGE=C \
  -DDEBUG_CONSOLE=UART

cmake --build armgcc/debug --target imx8mp_m7_caam_security.elf
```

Rebuild after a source edit:

```bash
cmake --build armgcc/debug --target imx8mp_m7_caam_security.elf
```

Clean:

```bash
rm -rf armgcc/debug
```

then configure again.

### 6.1 What you should get

| File | What it is |
|------|------------|
| `armgcc/debug/imx8mp_m7_caam_security.elf` | ELF with symbols (debug / `objdump`) |
| `armgcc/debug/imx8mp_m7_caam_security.bin` | Raw image U-Boot `fatload`s into TCM |

Copy the `.bin` to the EVK’s boot partition (or TFTP). Load address for
this project is **TCM at `0x7e0000`**, then `bootaux 0x7e0000`. That is a
**run** step, not a compile step.

### 6.2 Debug vs other presets

| Preset / `CMAKE_BUILD_TYPE` | Typical use |
|-----------------------------|-------------|
| `debug`                     | TCM, the image this project documents |
| `release`                   | TCM, `-O` / `NDEBUG` |
| `ddr_debug` / `ddr_release` | Link into DRAM (different linker script) |
| `flash_*`                   | Flash-resident (not how this demo is loaded) |

Stay on **`debug`** until you have a reason not to.

---

## 7. Optional: compile the Linux client

This is **not** `arm-none-eabi`. Build it **on the EVK** (AArch64 Linux)
or with a Linux AArch64 cross compiler:

```bash
cd crypto_rpmsg/linux
gcc -O2 -o m7_crypto_client m7_crypto_client.c
```

The client includes `../../m7_crypto_protocol.h` from the repo root. Run it
against `/dev/ttyRPMSG30` after the M7 is up and `rpmsg_tty` is loaded.

---

## 8. Common failures

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Please set ARMGCC_DIR` | Env not exported | `export ARMGCC_DIR=…` (the dir that **contains** `bin/`) |
| Cannot find `armgcc.cmake` | Wrong `SdkRootDirPath` | Point at the folder that contains `core/` |
| `arm-none-eabi-gcc: command not found` | `PATH` or `ARMGCC_DIR` | Put `$ARMGCC_DIR/bin` on `PATH` |
| Compiler is `aarch64-…` | Wrong toolchain | Use `arm-none-eabi`, not an AArch64 compiler |
| `-mcpu=cortex-m7` rejected | Toolchain is not M-profile | You grabbed an AArch64 compiler |
| Configure uses another user’s `/home/…` | `mcux_include.json` | Use the explicit `cmake -S … -B …` command in section 6 |
| Link errors in `rpmsg` / `FreeRTOS` | SDK missing middleware | Re-download SDK with multicore + FreeRTOS |
| `.elf` builds, no `.bin` | Post-build `objcopy` missing | Ensure `$ARMGCC_DIR/bin/arm-none-eabi-objcopy` exists |

`POSTPROCESS_UTILITY` / `mcux-fixelf` in `mcux_include.json` is an IDE
helper. The explicit CMake command in section 6 does not need it.

---

## 9. One-page recap

```text
Host:     Linux PC
Source:   git clone https://github.com/Its-Raata/imx8mp_m7_caam_security.git
Compiler: arm-none-eabi-gcc 14.2.Rel1  (Cortex-M7, Thumb, hard FPv5)
From:     MCUXpresso IDE 25.6.136  →  $ARMGCC_DIR/bin
     or   Arm GNU Toolchain 14.2.Rel1  (linux host, arm-none-eabi tarball)
SDK:      MCUXpresso SDK for MIMX8ML8, layout with core/  →  $SdkRootDirPath
Build:    cmake -S armgcc -B armgcc/debug … && cmake --build armgcc/debug
Output:   armgcc/debug/imx8mp_m7_caam_security.bin
```

You have compiled it when that `.bin` exists. Flashing, `bootaux`, CAAM
ownership, and the RPMsg tty are covered in the architecture and step docs.
