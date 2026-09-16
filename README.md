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
SYS / MDL to change a setting, the wheel push to select, and RTN or TELE to go
back.

In game:

- Right stick: push it forward to walk forward, sideways to turn
- Scroll wheel: step left and right
- SYS: fire
- Wheel push: fire
- RTN: open doors and use objects
- TELE: main menu
- Page Next: next weapon
- Page Prev: previous weapon
- MDL: next weapon

The stick is proportional: a gentle push walks, a full push runs. It is
centred at power-on, so don't hold it while the radio boots.

Note that EdgeTX ignores the wheel while its push button is held, so use SYS to
fire while stepping.

## Build

Same build instructions as EdgeTX.

Example:

```shell
mkdir -p build
cd build
cmake -DPCB=X10 -DPCBREV=TX16S -DDEFAULT_MODE=2 -DGVARS=YES -DPPM_UNIT=US -DHELI=NO -DLUA=NO -DCMAKE_BUILD_TYPE=Release -DGCC_ARM_PATH=$ARM/bin/ -DARM_TOOLCHAIN_DIR=$ARM/bin/ ../
make -j4 firmware
```
