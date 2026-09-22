## Wiring Guide for nice!nano and ProMicro/SuperMini nRF52840

> This table is for the **ST7789P3 172×320** panel this module currently drives, wired per
> `boards/nice_nano_nrf52840_zmk_2_0_0.overlay`. If you have an ST7789V 240×280 panel instead, this
> screen no longer supports it out of the box - the driver, devicetree binding and this pin table
> were all replaced with the P3 panel's own. The APDS9960 pins are unchanged from before the swap:
> only the display pins moved.

| ST7789P3 172x320 Display	    | nice!nano Pin |
|-------------------------------|-----------|
| VCC						    | VCC |
| GND						    | GND |
| SDA (MOSI) 				    | 101 |	
| SCL (SCK) 			    	| 106 |
| CS	    				    | 009 |
| DC	    	    		    | 107 |
| RST	           			    | 102 |
| BL						    | 010 |

| APDS9960 Light Sensor		    | nice!nano Pin |
|-------------------------------|-----------|
| VIN							| VCC |
| 3Vo							| No Connect |
| GND							| GND |
| SCL							| 020 |
| SDA							| 017 |
| INT							| 100 |

## Installation

Follow README installation and replace `seeeduino_xiao_ble` with `nice_nano_v2` for step 3 in your `build.yaml`.

<details>
<summary>Note for building with ZMK main (Zephyr 4.1)</summary>

Use `nice_nano@2.0.0//zmk` for step 3 in your `build.yaml`.

</details>

[3D print rear cap](/docs/3d_files/) modified and tested with SuperMini nRF52840.   
- Two versions, with and without reset button.  
- 6x6x6mm tactile button used for reset.  
- No supports needed.  
- Rear cap only. Print the display mount and main body from [Prospector](https://github.com/carrefinho/prospector/tree/main/case).

nice!nano fit untested, might be tight. Would like feedback.

> **The case files above were designed around the 240×280 ST7789V panel, not the 172×320 ST7789P3
> this module now drives.** The two are a different shape - the P3 panel is wider and shorter, not
> just a different resolution on the same glass - so the Prospector cutout and this rear cap are
> very unlikely to fit it without changes. Nobody has re-cut the case for the P3 panel yet.