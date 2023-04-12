# seekfree openart mini烧录方法

1. 打开`NXP-MCUBootUtility\bin\NXP-MCUBootUtility.exe`
2. 左上角`Target Setup`里, `MCU Series`选`i.MXRT`, `MCU Device`选`i.MXRT1064 SIP`, `Boot Device`选`FLEXSPI NOR`。
3. 用usb插上`openart mini`, 按住上面的`KEY1`键, 点一下`RST`键, 再松开`KEY1`键, 进到`boot烧写`模式。
4. 点左边中间的`Port Setup`里, 选`USB-HID`和下面的`One Step`, 之后点`Connect to ROM`。
5. 中间上面选`DEV Unsigned Image Boot`, 在中间的`Image Generation Sequence`里`Application Image File`选`bsp\imxrt\imxrt1064-seekfree-art-mini\build\keil\rtthread.axf`, 下面选`.out(axf) from Keil MDK`。
6. 点上面的`All-In-One Action`, 等烧写完成
7. 点左边的`Reset device`, 然后按`RST`键或重新上电就好了。