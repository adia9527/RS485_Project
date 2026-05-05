# 二、CubeMX 里最关键的配置

你这个项目需要配置这些部分：

```text
1. SYS 调试接口
2. RCC 时钟
3. GPIO
4. USART1
5. USART3
6. UART4
7. UART7
8. SPI1
9. SPI2
10. I2C1
11. I2C3
12. ADC1 + DMA
13. FreeRTOS
14. NVIC 中断
15. 工程代码生成设置
```

下面逐项说。

------

# 三、SYS 配置，非常重要

## 1. Debug 设置为 Serial Wire

在 CubeMX 里：

```text
System Core → SYS → Debug → Serial Wire
```

一定要选 **Serial Wire**，不要选 JTAG。

原因是你的引脚用了这些：

```text
PB3  → SPI1_SCK
PB4  → SPI1_MISO
PA15 → ESP32_EN
```

而这些脚默认和 JTAG 相关：

| 引脚 | 默认复用        |
| ---- | --------------- |
| PA13 | SWDIO           |
| PA14 | SWCLK           |
| PA15 | JTDI            |
| PB3  | JTDO / TRACESWO |
| PB4  | NJTRST          |

如果你开完整 JTAG，`PA15 / PB3 / PB4` 可能会被调试口占住，SPI1 和 ESP32_EN 就会出问题。

所以必须：

```text
Debug = Serial Wire
```

这样只保留：

```text
PA13 → SWDIO
PA14 → SWCLK
```

释放：

```text
PA15
PB3
PB4
```

这一步非常关键，属于“没配就会闹鬼”的那种。

------

# 四、RCC 时钟配置

## 1. 如果你板子有外部 8MHz 晶振

在 CubeMX：

```text
System Core → RCC
```

配置：

```text
High Speed Clock HSE → Crystal/Ceramic Resonator
Low Speed Clock LSE → Crystal/Ceramic Resonator，如果你焊了 32.768k 晶振
```

如果你没有焊 LSE，可以先不开。

------

## 2. 主频建议

STM32F429VET6 可以跑到 180MHz，但你项目没必要一上来拉满。

建议先配置：

```text
SYSCLK = 168MHz
APB1 = 42MHz
APB2 = 84MHz
```

这是 STM32F4 很常见、很稳的配置。

如果你用 8MHz HSE，可以配置成：

```text
HSE = 8MHz
PLL_M = 8
PLL_N = 336
PLL_P = 2
SYSCLK = 168MHz
PLL_Q = 7
```

大致是这样，CubeMX 时钟树会帮你检查。

------

## 3. FreeRTOS 项目建议修改 HAL Timebase

如果启用 FreeRTOS，CubeMX 常会提示 SysTick 被 FreeRTOS 使用。

建议：

```text
System Core → SYS → Timebase Source → TIM6
```

或者 TIM7 也可以。

推荐：

```text
Timebase Source = TIM6
```

这样：

```text
FreeRTOS 用 SysTick
HAL_Delay / HAL_GetTick 用 TIM6
```

避免两个系统抢同一个节拍钟。

------

# 五、GPIO 配置

先把所有普通控制脚配好。

------

## 1. RS485 方向控制 GPIO

### PA11

```text
PA11 → GPIO_Output
User Label: RS485_1_DIR
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: High 或 Very High
```

### PD10

```text
PD10 → GPIO_Output
User Label: RS485_2_DIR
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: High 或 Very High
```

默认低电平表示接收模式。

```text
DIR = 0：接收
DIR = 1：发送
```

------

## 2. TFT 控制引脚

### PD3

```text
PD3 → GPIO_Output
User Label: LCD_CS
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Very High
```

### PD4

```text
PD4 → GPIO_Output
User Label: LCD_DC
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Very High
```

### PD5

```text
PD5 → GPIO_Output
User Label: LCD_RST
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: High
```

### PD7

```text
PD7 → GPIO_Output
User Label: LCD_BL
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Low 或 Medium
```

背光先用普通 GPIO 控制开关即可。

注意：
**PD7 不一定方便做硬件 PWM 输出**，所以第一版建议只做：

```text
背光开
背光关
超时休眠
触摸 / 按键唤醒
```

不要一开始就做 PWM 调光。

------

## 3. W25Q64 片选

### PD2

```text
PD2 → GPIO_Output
User Label: W25Q64_CS
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Very High
```

因为 W25Q64 和 TFT 共用 SPI1，两个 CS 默认都必须拉高。

------

## 4. 触摸控制引脚

### PB8：触摸中断

推荐先配置成外部中断：

```text
PB8 → GPIO_EXTI8
User Label: CTP_INT
Mode: External Interrupt Mode with Falling edge trigger detection
Pull: Pull Up 或 No Pull
```

具体用 Pull Up 还是 No Pull，取决于触摸模块有没有外部上拉。
如果你不确定，先用：

```text
Pull Up
```

后面实际测 INT 空闲电平再调整。

### PB9：触摸复位

```text
PB9 → GPIO_Output
User Label: CTP_RST
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Medium
```

------

## 5. ESP32 控制引脚

### PA15

```text
PA15 → GPIO_Output
User Label: ESP32_EN
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Medium
```

### PC12

```text
PC12 → GPIO_Output
User Label: ESP32_IO0
Default Output Level: High
Mode: Output Push Pull
Pull: No Pull
Speed: Medium
```

一般：

```text
EN = 1：ESP32 运行
IO0 = 1：正常启动
IO0 = 0 + 复位：下载模式
```

第一版默认都拉高。

------

## 6. 蜂鸣器

### PC4

```text
PC4 → GPIO_Output
User Label: BEEP
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: Low
```

如果你的蜂鸣器电路是低电平有效，那默认值和控制逻辑要反过来。
这个需要看你的蜂鸣器驱动电路。

------

## 7. 按键

### PB0 / PB1 / PB2

如果你的按键是：

```text
按下接 GND
松开靠上拉为高电平
```

那么配置：

```text
PB0 → GPIO_Input
User Label: KEY1
Pull: Pull Up

PB1 → GPIO_Input
User Label: KEY2
Pull: Pull Up

PB2 → GPIO_Input
User Label: KEY3
Pull: Pull Up
```

按键逻辑：

```text
松开 = 1
按下 = 0
```

如果你的硬件外部已经加了上拉电阻，也可以 `No Pull`。
但第一版为了稳，内部上拉通常没问题。

------

## 8. LED

### PE10 / PE11 / PE12

```text
PE10 → GPIO_Output
User Label: LED1
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: Low

PE11 → GPIO_Output
User Label: LED2
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: Low

PE12 → GPIO_Output
User Label: LED3
Default Output Level: Low
Mode: Output Push Pull
Pull: No Pull
Speed: Low
```

注意：
如果 LED 是：

```text
MCU IO → 电阻 → LED → 3.3V
```

那就是低电平点亮。
如果是：

```text
3.3V → 电阻 → LED → MCU IO
```

也是低电平点亮。
如果是：

```text
MCU IO → 电阻 → LED → GND
```

就是高电平点亮。

所以代码里建议封装：

```c
BSP_LED_On()
BSP_LED_Off()
```

不要业务层直接写 GPIO 电平。

------

# 六、USART / UART 配置

你有 4 路串口：

```text
USART1 → Type-C 上位机 / 调试 / 命令 / 日志
USART3 → RS485_2
UART4  → ESP32
UART7  → RS485_1
```

------

## 1. USART1：Type-C 调试串口 / 上位机通信

CubeMX：

```text
Connectivity → USART1
Mode: Asynchronous
```

引脚：

```text
PA9  → USART1_TX
PA10 → USART1_RX
```

参数建议：

```text
Baud Rate: 9600
Word Length: 8 Bits
Parity: None
Stop Bits: 1
Mode: TX and RX
Hardware Flow Control: None
Over Sampling: 16
```

NVIC：

```text
USART1 global interrupt: Enable
```

当前代码使用 USART1 单字节中断接收命令；USART1 RX DMA 应保持禁用。

------

## 2. USART3：RS485_2

```text
Connectivity → USART3
Mode: Asynchronous
```

引脚：

```text
PD8 → USART3_TX
PD9 → USART3_RX
```

参数建议：

```text
Baud Rate: 9600
Word Length: 8 Bits
Parity: None
Stop Bits: 1
Mode: TX and RX
Hardware Flow Control: None
```

NVIC：

```text
USART3 global interrupt: Enable
```

USART3 用于 RS485_2 Modbus 收发。当前代码优先使用 USART3 RX DMA + IDLE：

```text
USART3_RX DMA: Enable
DMA mode: Circular
DMA interrupt: Enable
```

------

## 3. UART4：ESP32

```text
Connectivity → UART4
Mode: Asynchronous
```

引脚：

```text
PC10 → UART4_TX
PC11 → UART4_RX
```

参数建议：

```text
Baud Rate: 115200
Word Length: 8 Bits
Parity: None
Stop Bits: 1
Mode: TX and RX
Hardware Flow Control: None
```

NVIC：

```text
UART4 global interrupt: Enable
```

第一版可以只做 AT 指令发送接口，不急着完整解析 ESP32 返回。

------

## 4. UART7：RS485_1

```text
Connectivity → UART7
Mode: Asynchronous
```

引脚：

```text
PE8 → UART7_TX
PE7 → UART7_RX
```

参数建议：

```text
Baud Rate: 115200
Word Length: 8 Bits
Parity: None
Stop Bits: 1
Mode: TX and RX
Hardware Flow Control: None
```

NVIC：

```text
UART7 global interrupt: Enable
```

UART7 用于 RS485_1 Modbus 收发，不用于日志、命令或本地上传。建议配置 UART7 RX DMA + IDLE：

```text
UART7_RX DMA: Enable
DMA mode: Circular
```

它就是你项目的“黑匣子小喇叭”。

------

# 七、SPI 配置

你有两路 SPI：

```text
SPI1 → TFT + W25Q64
SPI2 → SPI 传感器扩展
```

------

## 1. SPI1：TFT + W25Q64

CubeMX：

```text
Connectivity → SPI1
Mode: Full-Duplex Master
```

引脚：

```text
PB3 → SPI1_SCK
PB4 → SPI1_MISO
PB5 → SPI1_MOSI
```

参数建议第一版：

```text
Mode: Full-Duplex Master
Hardware NSS: Disable
Data Size: 8 Bits
First Bit: MSB First
Prescaler: 16 或 32
Clock Polarity: Low
Clock Phase: 1 Edge
CRC Calculation: Disabled
NSS Signal Type: Software
```

也就是常见 SPI Mode 0：

```text
CPOL = 0
CPHA = 0
```

初期 Prescaler 不要太快，先用：

```text
Prescaler = 16 或 32
```

等 TFT 和 W25Q64 都稳定后，再提高速度。

注意：

```text
LCD_CS 和 W25Q64_CS 都用 GPIO 手动控制
SPI1 的 NSS 不要交给硬件
```

------

## 2. SPI2：SPI 传感器扩展

CubeMX：

```text
Connectivity → SPI2
Mode: Full-Duplex Master
```

引脚：

```text
PB13 → SPI2_SCK
PB14 → SPI2_MISO
PB15 → SPI2_MOSI
PB12 → SPI2_CS，GPIO_Output
```

参数建议：

```text
Hardware NSS: Disable
Data Size: 8 Bits
Prescaler: 32
CPOL: Low
CPHA: 1 Edge
First Bit: MSB First
```

PB12：

```text
PB12 → GPIO_Output
User Label: SPI2_CS
Default Output Level: High
```

如果暂时没有 SPI 传感器，也可以先不开 SPI2，但我建议开着，因为你 IO 已经规划好了，Claude 后面生成 BSP 会更顺。

------

# 八、I2C 配置

你有两路 I2C：

```text
I2C1 → 触摸屏
I2C3 → I2C 传感器扩展
```

------

## 1. I2C1：触摸屏

CubeMX：

```text
Connectivity → I2C1
Mode: I2C
```

引脚：

```text
PB6 → I2C1_SCL
PB7 → I2C1_SDA
```

参数建议：

```text
I2C Speed Mode: Fast Mode
I2C Clock Speed: 400000
Addressing Mode: 7-bit
Dual Address: Disabled
General Call: Disabled
No Stretch: Disabled
```

如果触摸芯片不稳定，可以改成：

```text
100000
```

硬件上 I2C 必须有上拉电阻，一般：

```text
4.7kΩ 上拉到 3.3V
```

内部上拉通常偏弱，不建议只靠内部上拉。

------

## 2. I2C3：扩展传感器

CubeMX：

```text
Connectivity → I2C3
Mode: I2C
```

引脚：

```text
PA8 → I2C3_SCL
PC9 → I2C3_SDA
```

参数建议：

```text
I2C Clock Speed: 100000 或 400000
Addressing Mode: 7-bit
```

第一版建议 100k，更稳：

```text
100000
```

------

# 九、ADC 配置

你规划了：

```text
PA3 → ADC
PA4 → ADC
PA5 → ADC
PA6 → ADC
```

在 STM32F429 上可以使用 ADC1：

| 引脚 | ADC 通道 |
| ---- | -------- |
| PA3  | ADC1_IN3 |
| PA4  | ADC1_IN4 |
| PA5  | ADC1_IN5 |
| PA6  | ADC1_IN6 |

------

## 1. CubeMX 中开启 ADC1

```text
Analog → ADC1
```

选择通道：

```text
IN3
IN4
IN5
IN6
```

------

## 2. ADC 参数建议

如果你想让 Claude 后面写 DMA 采集，建议 CubeMX 直接配好 ADC + DMA。

ADC1 配置：

```text
Resolution: 12 bits
Scan Conversion Mode: Enabled
Continuous Conversion Mode: Enabled
Discontinuous Conversion Mode: Disabled
External Trigger Conversion Source: Regular Conversion launched by software
Data Alignment: Right alignment
Number Of Conversion: 4
DMA Continuous Requests: Enabled
End Of Conversion Selection: EOC flag at the end of all conversions
```

Rank 配置：

```text
Rank 1 → Channel 3，PA3
Rank 2 → Channel 4，PA4
Rank 3 → Channel 5，PA5
Rank 4 → Channel 6，PA6
```

采样时间建议选长一点：

```text
Sampling Time: 144 cycles 或 480 cycles
```

尤其是你做 12V 分压检测时，如果分压电阻比较大，例如 100k / 27k，ADC 输入源阻抗偏高。采样时间太短，读数可能飘。
所以推荐：

```text
480 cycles
```

别嫌慢，环境采集不是高速示波器，慢一点像老茶壶出水，稳。

------

## 3. ADC DMA 配置

在 ADC1 的 DMA Settings 里添加 DMA：

```text
DMA Request: ADC1
Mode: Circular
Data Width Peripheral: Half Word
Data Width Memory: Half Word
Increment Address Peripheral: Disabled
Increment Address Memory: Enabled
Priority: Low 或 Medium
```

这样代码里可以：

```c
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, 4);
```

后台循环采样 4 个通道。

------

## 4. ADC 通道用途建议

| ADC 通道 | 引脚 | 用途                |
| -------- | ---- | ------------------- |
| ADC1_IN3 | PA3  | 12V 输入电压检测    |
| ADC1_IN4 | PA4  | AIN1 外部模拟量输入 |
| ADC1_IN5 | PA5  | AIN2 外部模拟量输入 |
| ADC1_IN6 | PA6  | 光敏电阻 / 电位器   |

------

# 十、FreeRTOS 配置

CubeMX：

```text
Middleware and Software Packs → FREERTOS
```

建议选择：

```text
Interface: CMSIS_V2
```

或者 CMSIS_V1 也可以，但 CMSIS_V2 更现代一些。

------

## 1. FreeRTOS 基础参数

建议：

```text
USE_PREEMPTION = Enabled
TICK_RATE_HZ = 1000
MAX_PRIORITIES = 56 默认可不动
MINIMAL_STACK_SIZE = 128 words
TOTAL_HEAP_SIZE = 32KB 或更高
```

STM32F429VET6 RAM 还算够，建议 FreeRTOS heap 先给大一点：

```text
TOTAL_HEAP_SIZE = 48KB 或 64KB
```

后面用了 LVGL，内存要留足。

------

# 十一、NVIC 中断配置

需要注意 FreeRTOS 的中断优先级。

建议开启：

```text
USART1 global interrupt
USART3 global interrupt
UART4 global interrupt
UART7 global interrupt
EXTI line[9:5] interrupt，因为 PB8 触摸中断在 EXTI8
DMA2 Stream0 或 CubeMX 自动分配的 ADC DMA 中断
```

具体 ADC DMA 用哪个 Stream，CubeMX 会自动生成。

------

## 中断优先级建议

对于 STM32F4 + FreeRTOS，常见设置是：

```text
NVIC Priority Group = 4
```



------

# 你的 IO 配置总表

你可以照这个逐项检查 CubeMX。

| 功能        | 引脚 | CubeMX 配置         |
| ----------- | ---- | ------------------- |
| CONSOLE_TX  | PA9  | USART1_TX           |
| CONSOLE_RX  | PA10 | USART1_RX           |
| RS485_1_TX  | PE8  | UART7_TX            |
| RS485_1_RX  | PE7  | UART7_RX            |
| RS485_1_DIR | PA11 | GPIO_Output         |
| RS485_2_TX  | PD8  | USART3_TX           |
| RS485_2_RX  | PD9  | USART3_RX           |
| RS485_2_DIR | PD10 | GPIO_Output         |
| TFT_SCK     | PB3  | SPI1_SCK            |
| TFT_MISO    | PB4  | SPI1_MISO           |
| TFT_MOSI    | PB5  | SPI1_MOSI           |
| LCD_CS      | PD3  | GPIO_Output，默认高 |
| LCD_DC      | PD4  | GPIO_Output         |
| LCD_RST     | PD5  | GPIO_Output，默认高 |
| LCD_BL      | PD7  | GPIO_Output，默认高 |
| TOUCH_SCL   | PB6  | I2C1_SCL            |
| TOUCH_SDA   | PB7  | I2C1_SDA            |
| TOUCH_INT   | PB8  | GPIO_EXTI8          |
| TOUCH_RST   | PB9  | GPIO_Output，默认高 |
| W25Q64_CS   | PD2  | GPIO_Output，默认高 |
| I2C3_SCL    | PA8  | I2C3_SCL            |
| I2C3_SDA    | PC9  | I2C3_SDA            |
| SPI2_SCK    | PB13 | SPI2_SCK            |
| SPI2_MISO   | PB14 | SPI2_MISO           |
| SPI2_MOSI   | PB15 | SPI2_MOSI           |
| SPI2_CS     | PB12 | GPIO_Output，默认高 |
| ADC1_IN3    | PA3  | ADC1_IN3            |
| ADC1_IN4    | PA4  | ADC1_IN4            |
| ADC1_IN5    | PA5  | ADC1_IN5            |
| ADC1_IN6    | PA6  | ADC1_IN6            |
| ESP32_TX    | PC10 | UART4_TX            |
| ESP32_RX    | PC11 | UART4_RX            |
| ESP32_EN    | PA15 | GPIO_Output，默认高 |
| ESP32_IO0   | PC12 | GPIO_Output，默认高 |
| BEEP        | PC4  | GPIO_Output         |
| KEY1        | PB0  | GPIO_Input，上拉    |
| KEY2        | PB1  | GPIO_Input，上拉    |
| KEY3        | PB2  | GPIO_Input，上拉    |
| LED1        | PE10 | GPIO_Output         |
| LED2        | PE11 | GPIO_Output         |
| LED3        | PE12 | GPIO_Output         |
| SWDIO       | PA13 | SYS_JTMS-SWDIO      |
| SWCLK       | PA14 | SYS_JTCK-SWCLK      |
| NRST        | NRST | Reset               |
