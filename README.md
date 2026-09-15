# Doom for RadioMaster TX16S

## Installation

It should not change any settings on the radio or the SD Card, but it's still recommended to create a backup first.

- Copy the file `doom_tx16s-xxxxxx_fw.bin` to the SD Card under `FIRMWARE`
- Copy the folder `DOOM` to the SD Card
- Power off the radio
- Enter bootloader pushing both trim buttons (T4 and T1) inwards (towards the powerbutton) while pressing and holding the powerbutton
- Select write the firmware you just copied to the SD Card
- Reboot

## Uninstallation

- Do the same as you would do to install an EdgeTx updated Firmware.

## Interaction

In the menus, use the scroll wheel or Page Next / Page Prev to move the cursor,
SYS / MDL to change a setting, and Enter / Back to navigate.

In game:

- Scroll wheel: turn left and right
- Page Next: move forward
- Page Prev: move backward
- SYS: fire
- Enter (wheel push): fire
- MDL: next weapon
- TELE: Open doors and use objects

Turn speed follows how fast the wheel is spun, and the mouse sensitivity slider
in Doom's options menu scales it.

Note that EdgeTX ignores the wheel while its push button is held, so use SYS to
fire while turning.

## Build

Same build instructions as EdgeTX.

Example:

```shell
mkdir -p build
cd build
cmake -DPCB=X10 -DPCBREV=TX16S -DDEFAULT_MODE=2 -DGVARS=YES -DPPM_UNIT=US -DHELI=NO -DLUA=NO -DCMAKE_BUILD_TYPE=Release -DGCC_ARM_PATH=$ARM/bin/ -DARM_TOOLCHAIN_DIR=$ARM/bin/ ../
make -j4 firmware
```
