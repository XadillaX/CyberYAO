// components/bsp/src/bsp_power.c
// 见 bsp_power.h：以 USB Serial/JTAG 主机连接状态近似充电检测。
#include "bsp_power.h"
#include "driver/usb_serial_jtag.h"

bool bsp_power_is_charging(void) {
  return usb_serial_jtag_is_connected();
}
