```text
PROJECT_REQUIREMENTS.md
```

然后丢到你的 STM32 工程根目录里，让 Claude Code 先读这个文件。

------

~~~markdown
# 基于 STM32F429VET6 的 RS485 多传感器环境数据采集与本地告警终端项目说明

## 1. 项目定位

本项目是一个基于 STM32F429VET6 + FreeRTOS 的多传感器环境数据采集与本地告警终端。

系统主要面向智能家居、小型楼宇、机房环境监测等场景，通过 RS485 Modbus RTU 总线采集多个环境传感器数据，并通过 TFT 彩屏进行本地显示，同时支持本地按键交互、阈值告警、参数保存、上位机串口通信、ESP32 网络扩展接口、ADC 模拟量扩展采集等功能。

项目重点不是单纯读取传感器，而是实现一个较完整的嵌入式采集终端，体现以下能力：

- STM32 外设驱动
- FreeRTOS 多任务设计
- RS485 半双工通信
- Modbus RTU 主机协议
- SPI TFT 显示
- I2C 触摸
- LVGL 图形界面
- W25Q64 外部 Flash 数据存储
- ADC 多通道采集
- 蜂鸣器 / LED 告警
- 按键人机交互
- 串口上位机通信
- ESP32 WiFi 扩展
- 模块化软件架构设计

---

## 2. 主控芯片

主控芯片型号：

```text
STM32F429VET6
~~~

工程开发环境：

```text
STM32CubeMX + VS Code + CMake 
HAL 库
FreeRTOS
```

当前希望 Claude Code 生成的代码以 HAL + FreeRTOS 风格为主，尽量保持模块化，不要把所有逻辑塞进 main.c。

------

# 3. 硬件 IO 分配

## 3.1 RS485 接口

系统有两路 RS485 接口，用于连接多个 Modbus RTU 从站传感器。

### RS485_1

| 功能        | 引脚 | 说明               |
| ----------- | ---- | ------------------ |
| UART7_TX    | PE8  | RS485_1 发送       |
| UART7_RX    | PE7  | RS485_1 接收       |
| RS485_1_DIR | PA11 | RS485 收发方向控制 |

方向控制规则：

```text
DIR = 1：发送模式
DIR = 0：接收模式
```

### RS485_2

| 功能        | 引脚 | 说明               |
| ----------- | ---- | ------------------ |
| USART3_TX   | PD8  | RS485_2 发送       |
| USART3_RX   | PD9  | RS485_2 接收       |
| RS485_2_DIR | PD10 | RS485 收发方向控制 |

方向控制规则：

```text
DIR = 1：发送模式
DIR = 0：接收模式
```

------

## 3.2 TFT 彩屏接口

TFT 显示部分使用 SPI1。

### TFT LCD 显示接口

| 功能      | 引脚 | 说明                          |
| --------- | ---- | ----------------------------- |
| SPI1_SCK  | PB3  | TFT SPI 时钟                  |
| SPI1_MISO | PB4  | TFT SPI MISO，很多 TFT 可不用 |
| SPI1_MOSI | PB5  | TFT SPI MOSI                  |
| LCD_CS    | PD3  | TFT 片选                      |
| LCD_DC    | PD4  | 数据 / 命令选择               |
| LCD_RST   | PD5  | TFT 复位                      |
| LCD_BL    | PD7  | TFT 背光控制                  |

注意：

- TFT 显示部分走 SPI。
- LCD_BL 是背光控制线，可以使用普通 GPIO 控制开关，也可以后续改成 PWM 调光。
- 关闭背光不影响 SPI 显示芯片本身工作，也不影响触摸芯片工作。背光只是照明，屏幕内容和触摸通信仍可独立运行。
- 可以实现长时间无操作后关闭背光，触摸中断触发后重新打开背光。

------

## 3.3 触摸屏接口

触摸部分使用 I2C1。

| 功能     | 引脚 | 说明          |
| -------- | ---- | ------------- |
| I2C1_SCL | PB6  | 触摸 I2C 时钟 |
| I2C1_SDA | PB7  | 触摸 I2C 数据 |
| CTP_INT  | PB8  | 触摸中断输入  |
| CTP_RST  | PB9  | 触摸芯片复位  |

说明：

- 触摸芯片通过 I2C1 通信。
- CTP_INT 用于检测触摸事件。
- CTP_RST 用于复位触摸芯片。
- 即使 LCD_BL 关闭，触摸芯片仍应保持供电和工作。
- 当检测到 CTP_INT 触发或读取到触摸事件时，应唤醒背光。

------

## 3.4 W25Q64 外部 Flash

W25Q64 与 TFT 共享 SPI1 总线，但使用独立片选。

| 功能      | 引脚 | 说明        |
| --------- | ---- | ----------- |
| SPI1_SCK  | PB3  | 与 TFT 共用 |
| SPI1_MISO | PB4  | 与 TFT 共用 |
| SPI1_MOSI | PB5  | 与 TFT 共用 |
| W25Q64_CS | PD2  | W25Q64 片选 |

注意：

- TFT 和 W25Q64 共用 SPI1，必须通过不同 CS 区分。
- 操作 TFT 时，W25Q64_CS 必须拉高。
- 操作 W25Q64 时，LCD_CS 必须拉高。
- 后续可用于保存 UI 图片资源、字体资源、历史数据、告警日志等。

------

## 3.5 I2C 传感器扩展接口

| 功能     | 引脚 | 说明               |
| -------- | ---- | ------------------ |
| I2C3_SCL | PA8  | I2C 传感器扩展时钟 |
| I2C3_SDA | PC9  | I2C 传感器扩展数据 |

说明：

- 该接口用于扩展 I2C 传感器。
- 当前主传感器以 RS485 为主，I2C3 作为预留扩展。

------

## 3.6 SPI 传感器扩展接口

| 功能      | 引脚 | 说明            |
| --------- | ---- | --------------- |
| SPI2_SCK  | PB13 | SPI 传感器时钟  |
| SPI2_MISO | PB14 | SPI 传感器 MISO |
| SPI2_MOSI | PB15 | SPI 传感器 MOSI |
| SPI2_CS   | PB12 | SPI 传感器片选  |

说明：

- 该接口用于扩展 SPI 类型传感器。
- 当前可以先预留 BSP 接口，不一定马上实现具体传感器驱动。

------

## 3.7 ADC 模拟量输入

| 功能    | 引脚 | 说明         |
| ------- | ---- | ------------ |
| ADC_CH1 | PA3  | 模拟量输入 1 |
| ADC_CH2 | PA4  | 模拟量输入 2 |
| ADC_CH3 | PA5  | 模拟量输入 3 |
| ADC_CH4 | PA6  | 模拟量输入 4 |

ADC 功能定位：

```text
RS485 负责主环境传感器采集；
ADC 负责系统自监控和模拟量扩展输入。
```

建议功能：

| ADC 通道 | 建议用途                  |
| -------- | ------------------------- |
| PA3      | 12V 输入电压检测          |
| PA4      | 外部模拟量输入 AIN1       |
| PA5      | 外部模拟量输入 AIN2       |
| PA6      | 板载光敏电阻 / 电位器输入 |

ADC 软件需要实现：

- 原始 ADC 值读取
- 电压换算
- 滑动平均或低通滤波
- 输入电压检测
- 模拟量阈值判断
- 异常告警
- 上传上位机
- TFT 页面显示

------

## 3.8 ESP32 通信接口

| 功能      | 引脚 | 说明               |
| --------- | ---- | ------------------ |
| UART4_TX  | PC10 | STM32 发送到 ESP32 |
| UART4_RX  | PC11 | STM32 接收 ESP32   |
| ESP32_EN  | PA15 | ESP32 使能         |
| ESP32_IO0 | PC12 | ESP32 下载模式控制 |

说明：

- ESP32 可用于 WiFi / MQTT / HTTP 上传。
- 当前阶段可以先实现 UART4 基础通信接口。
- 后续可以通过 AT 指令控制 ESP32 入网和上传数据。
- ESP32_EN 可用于硬件复位或使能。
- ESP32_IO0 可用于进入下载模式。

------

## 3.9 Type-C 调试串口

| 功能      | 引脚 | 说明                |
| --------- | ---- | ------------------- |
| USART1_TX | PA9  | Type-C 调试串口发送 |
| USART1_RX | PA10 | Type-C 调试串口接收 |

说明：

- USART1 用于连接上位机或串口助手。
- 支持输出实时数据、告警状态、通信状态、系统日志。
- 后续也可支持上位机下发参数。

------

## 3.10 蜂鸣器

| 功能 | 引脚 | 说明       |
| ---- | ---- | ---------- |
| BEEP | PC4  | 蜂鸣器控制 |

蜂鸣器功能：

- 超限告警提示
- 通信异常提示
- 按键确认提示
- 上电自检短响
- 告警静音控制

------

## 3.11 按键

| 功能 | 引脚 | 说明   |
| ---- | ---- | ------ |
| KEY1 | PB0  | 按键 1 |
| KEY2 | PB1  | 按键 2 |
| KEY3 | PB2  | 按键 3 |

按键功能建议：

| 按键             | 普通页面     | 设置页面     |
| ---------------- | ------------ | ------------ |
| KEY1 短按        | 下一页       | 切换参数项   |
| KEY1 长按        | 进入设置页面 | 保存并退出   |
| KEY2 短按        | 上一页       | 参数增加     |
| KEY3 短按        | 静音 / 确认  | 参数减少     |
| KEY2 + KEY3 长按 | 恢复默认参数 | 恢复默认参数 |

需要实现：

- 消抖
- 短按识别
- 长按识别
- 组合键识别
- 按键事件队列

------

## 3.12 LED 指示灯

| 功能 | 引脚 | 说明         |
| ---- | ---- | ------------ |
| LED1 | PE10 | 系统运行指示 |
| LED2 | PE11 | 通信状态指示 |
| LED3 | PE12 | 告警状态指示 |

建议功能：

| LED  | 功能                             |
| ---- | -------------------------------- |
| LED1 | 系统心跳，正常运行时 1s 闪烁     |
| LED2 | RS485 通信时闪烁，通信异常时慢闪 |
| LED3 | 告警时闪烁，无告警熄灭           |

------

# 4. 传感器规划

## 4.1 RS485 Modbus 传感器

主传感器全部采用 RS485 Modbus RTU。

| 传感器         | 接口  | 协议       | 默认地址 | 数据           |
| -------------- | ----- | ---------- | -------- | -------------- |
| 温湿度传感器   | RS485 | Modbus RTU | 0x01     | 温度、湿度     |
| 人体存在传感器 | RS485 | Modbus RTU | 0x02     | 是否有人、状态 |
| 光照传感器     | RS485 | Modbus RTU | 0x03     | 光照强度 lux   |
| CO2 传感器     | RS485 | Modbus RTU | 0x04     | CO2 ppm        |

说明：

- 当前优先使用 RS485_1 轮询所有传感器。
- RS485_2 可以作为备用总线或扩展总线。
- 传感器寄存器地址可以先用宏定义，后续根据实际传感器手册修改。

------

## 4.2 Modbus RTU 主机功能

需要实现 Modbus RTU 主机，不需要实现从机。

必须支持：

- 功能码 0x03：读保持寄存器
- 功能码 0x04：读输入寄存器，可选
- CRC16 校验
- 请求帧封装
- 响应帧解析
- 地址校验
- 功能码校验
- 异常响应处理
- 通信超时
- 重试机制
- 连续失败后判断离线

典型流程：

```text
切换 RS485 为发送模式
发送 Modbus 请求帧
等待 UART 发送完成
切换 RS485 为接收模式
等待从站响应
判断是否超时
校验 CRC
解析数据
更新传感器状态
```

------

# 5. 功能需求

## 5.1 数据采集功能

系统需要采集以下数据：

| 数据              | 来源                 |
| ----------------- | -------------------- |
| 温度              | RS485 温湿度传感器   |
| 湿度              | RS485 温湿度传感器   |
| 人体存在          | RS485 人体存在传感器 |
| 光照强度          | RS485 光照传感器     |
| CO2 浓度          | RS485 CO2 传感器     |
| 输入电压          | ADC                  |
| AIN1 模拟量       | ADC                  |
| AIN2 模拟量       | ADC                  |
| 本地光敏 / 电位器 | ADC                  |

每类数据都需要有：

- 当前值
- 上次更新时间
- 数据是否有效
- 通信状态
- 异常状态

------

## 5.2 本地显示功能

显示设备为 TFT 彩屏，使用 LVGL 绘制界面。

建议页面：

### 主页

显示：

- 温度
- 湿度
- CO2
- 光照
- 人体存在状态
- 总告警状态
- 通信状态概览

### 传感器详情页

显示：

- 每个传感器当前值
- 从站地址
- 在线 / 离线状态
- 最后更新时间

### 通信状态页

显示：

- RS485_1 状态
- RS485_2 状态
- 各从站通信状态
- 超时计数
- CRC 错误计数

### 告警状态页

显示：

- 当前告警源
- 告警等级
- 告警值
- 阈值
- 蜂鸣器是否静音

### 参数设置页

显示并允许修改：

- 温度上限 / 下限
- 湿度上限 / 下限
- CO2 上限
- 光照上限 / 下限
- 报警使能
- 蜂鸣器使能
- 上传周期
- 轮询周期

### ADC 模拟量页

显示：

- 输入电压 Vin
- AIN1 电压
- AIN2 电压
- 光敏 / 电位器输入值
- 电源状态

### 系统信息页

显示：

- 固件版本
- 运行时间
- FreeRTOS 剩余堆内存
- 设备 ID
- 系统状态

------

## 5.3 背光控制功能

TFT 背光由 LCD_BL 控制。

需要实现：

- 上电默认打开背光
- 长时间无按键、无触摸操作后自动关闭背光
- 按键操作时重新打开背光
- 触摸中断触发时重新打开背光
- 可根据 ADC 光敏值调节背光亮度，后续扩展

说明：

- LCD_BL 只控制背光，不应关闭 TFT 控制器和触摸芯片供电。
- 关闭背光后，触摸 I2C 和 CTP_INT 仍应正常工作。
- 如果 CTP_INT 检测到触摸事件，应立即打开背光。

建议参数：

```text
BACKLIGHT_TIMEOUT_MS = 30000
```

------

## 5.4 告警功能

系统支持多种告警源。

### 告警源

```text
温度过高
温度过低
湿度过高
湿度过低
CO2 过高
光照过强
光照过低
人体存在异常
RS485 通信异常
传感器离线
输入电压过低
ADC 模拟量超限
```

### 告警输出

- TFT 页面显示 ALARM
- 蜂鸣器响
- LED3 闪烁
- USART1 上传告警日志
- 可选：ESP32 上传告警

### 告警机制

需要支持：

- 告警使能
- 蜂鸣器使能
- 告警静音
- 告警回差
- 告警恢复判断
- 告警等级

建议告警等级：

```c
typedef enum
{
    ALARM_LEVEL_NONE = 0,
    ALARM_LEVEL_WARN,
    ALARM_LEVEL_ALARM,
    ALARM_LEVEL_DANGER
} AlarmLevel_t;
```

------

## 5.5 参数保存功能

系统参数需要掉电保存。

优先使用 STM32 内部 Flash 保存配置参数。
后续也可以扩展为 W25Q64 保存历史数据和资源文件。

需要保存的参数：

- 温度上限 / 下限
- 湿度上限 / 下限
- CO2 上限
- 光照上限 / 下限
- 输入电压低压阈值
- ADC 模拟量阈值
- RS485 从站地址
- 轮询周期
- 上传周期
- 背光超时时间
- 告警使能
- 蜂鸣器使能
- 设备 ID

配置结构体需要包含：

- Magic Number
- Version
- 参数内容
- CRC32 或 checksum

示例：

```c
#define CONFIG_MAGIC   0xA5A55A5A
#define CONFIG_VERSION 0x0001

typedef struct
{
    uint32_t magic;
    uint16_t version;

    float temp_max;
    float temp_min;
    float humi_max;
    float humi_min;

    uint16_t co2_max;
    uint16_t light_max;
    uint16_t light_min;

    float vin_low_threshold;
    float ain1_high_threshold;
    float ain2_high_threshold;

    uint8_t temp_humi_addr;
    uint8_t human_addr;
    uint8_t light_addr;
    uint8_t co2_addr;

    uint32_t poll_period_ms;
    uint32_t upload_period_ms;
    uint32_t backlight_timeout_ms;

    uint8_t alarm_enable;
    uint8_t buzzer_enable;
    uint8_t device_id;

    uint32_t checksum;
} SystemConfig_t;
```

读取流程：

```text
上电读取 Flash 配置
检查 magic
检查 version
检查 checksum
如果有效：加载配置
如果无效：加载默认配置
```

写入策略：

- 参数变化后设置 config_dirty 标志
- 延时几秒后统一保存
- 不要每次按键变化都立即写 Flash

------

## 5.6 上位机通信功能

通过 USART1 Type-C 调试串口与上位机通信。

### 基础上传格式

可以先使用文本格式：

```text
TEMP=26.5,HUMI=63.2,CO2=680,LIGHT=350,HUMAN=1,VIN=12.1,AIN1=1.25,AIN2=2.80,ALARM=0
```

### 推荐 JSON 格式

后续推荐输出一行 JSON，方便 C# WPF 上位机解析：

```json
{"id":1,"temp":26.5,"humi":63.2,"co2":680,"light":350,"human":1,"vin":12.1,"ain1":1.25,"ain2":2.8,"alarm":0}
```

### 日志类型

需要支持：

```text
[DATA] 实时数据
[ALARM] 告警事件
[COMM] 通信状态
[CONFIG] 参数变化
[SYS] 系统状态
[DEBUG] 调试日志
```

------

## 5.7 ESP32 扩展功能

当前阶段只需要预留接口。

后续可以实现：

- ESP32 AT 指令通信
- WiFi 连接
- MQTT 上传
- HTTP 上传
- 远程参数下发

当前代码中可以先提供：

```c
void ESP32_Init(void);
void ESP32_Reset(void);
void ESP32_Enable(void);
void ESP32_Disable(void);
HAL_StatusTypeDef ESP32_SendAT(const char *cmd);
```

------

# 6. FreeRTOS 任务设计

建议使用以下任务。

## 6.1 SensorTask

职责：

- 采集 ADC 数据
- 轮询 RS485 Modbus 传感器
- 更新传感器数据
- 更新通信状态
- 统计通信错误
- 设置数据更新事件

建议周期：

```text
100ms
```

内部用状态机和时间戳控制不同传感器轮询周期。

------

## 6.2 DisplayTask

职责：

- 初始化 LVGL 页面
- 周期调用 LVGL 刷新
- 根据传感器数据更新 UI
- 处理页面切换
- 显示告警状态
- 显示通信状态
- 显示 ADC 数据

建议：

- LVGL 的 `lv_timer_handler()` 应在专门任务中周期执行。
- 周期可设置为 5ms - 10ms。
- 实际 UI 数据刷新可 200ms - 500ms 一次。

------

## 6.3 KeyTask

职责：

- 扫描 KEY1 / KEY2 / KEY3
- 消抖
- 短按 / 长按 / 组合键判断
- 发送按键事件到队列
- 更新最后操作时间，用于背光控制

建议周期：

```text
10ms
```

------

## 6.4 TouchTask

职责：

- 检测 CTP_INT
- 通过 I2C1 读取触摸点
- 将触摸坐标传给 LVGL
- 触摸时唤醒背光
- 更新最后操作时间

说明：

- 如果暂时没有触摸驱动，可以先写空框架。
- 后续根据实际触摸芯片型号补充驱动，例如 GT911 / CST816 / FT6336 等。

------

## 6.5 AlarmTask

职责：

- 根据传感器数据判断告警
- 根据 ADC 数据判断电源异常
- 处理告警回差
- 控制蜂鸣器
- 控制 LED3
- 记录告警事件
- 通知 DisplayTask / UploadTask

建议周期：

```text
200ms
```

------

## 6.6 UploadTask

职责：

- 周期通过 USART1 上传实时数据
- 上传告警状态
- 上传通信状态
- 上传系统状态
- 后续解析上位机下发命令

建议周期：

```text
1000ms
```

------

## 6.7 ConfigTask

职责：

- 管理系统配置参数
- 检测 config_dirty
- 延时保存 Flash
- 恢复默认参数
- 校验参数合法性

建议周期：

```text
500ms
```

------

## 6.8 BacklightTask

职责：

- 管理 LCD_BL
- 检测无操作超时
- 自动关闭背光
- 按键 / 触摸唤醒背光
- 后续支持 ADC 光敏调节亮度

建议周期：

```text
200ms
```

------

## 6.9 MonitorTask

职责：

- 系统心跳 LED1
- 统计运行时间
- 输出系统状态
- 检查剩余堆内存
- 后续喂看门狗

建议周期：

```text
1000ms
```

------

# 7. 数据结构建议

## 7.1 传感器状态

```c
typedef enum
{
    SENSOR_STATUS_OFFLINE = 0,
    SENSOR_STATUS_ONLINE,
    SENSOR_STATUS_TIMEOUT,
    SENSOR_STATUS_CRC_ERROR,
    SENSOR_STATUS_DATA_ERROR
} SensorStatus_t;
```

------

## 7.2 传感器数据

```c
typedef struct
{
    float temperature;
    float humidity;
    uint16_t co2;
    uint16_t light;
    uint8_t human_exist;

    SensorStatus_t temp_humi_status;
    SensorStatus_t human_status;
    SensorStatus_t light_status;
    SensorStatus_t co2_status;

    uint32_t last_update_tick;
} SensorData_t;
```

------

## 7.3 ADC 数据

```c
typedef struct
{
    uint16_t raw;
    float voltage;
    float value;
    uint8_t valid;
} AnalogChannel_t;

typedef struct
{
    AnalogChannel_t vin;
    AnalogChannel_t ain1;
    AnalogChannel_t ain2;
    AnalogChannel_t local_light;
} AnalogData_t;
```

------

## 7.4 通信统计

```c
typedef struct
{
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t timeout_count;
    uint32_t crc_error_count;
    uint32_t data_error_count;
} CommStat_t;
```

------

## 7.5 告警状态

```c
typedef enum
{
    ALARM_SRC_NONE = 0,
    ALARM_SRC_TEMP_HIGH,
    ALARM_SRC_TEMP_LOW,
    ALARM_SRC_HUMI_HIGH,
    ALARM_SRC_HUMI_LOW,
    ALARM_SRC_CO2_HIGH,
    ALARM_SRC_LIGHT_HIGH,
    ALARM_SRC_LIGHT_LOW,
    ALARM_SRC_HUMAN_DETECT,
    ALARM_SRC_COMM_FAIL,
    ALARM_SRC_POWER_LOW,
    ALARM_SRC_AIN1_HIGH,
    ALARM_SRC_AIN2_HIGH
} AlarmSource_t;

typedef struct
{
    uint8_t active;
    AlarmSource_t source;
    AlarmLevel_t level;
    float current_value;
    float threshold;
    uint8_t muted;
    uint32_t trigger_tick;
} AlarmState_t;
```

------

# 8. 软件模块划分建议

请尽量按照 BSP 层、协议层、应用层分层。

## 8.1 BSP 层

```text
bsp_led.c / bsp_led.h
bsp_key.c / bsp_key.h
bsp_beep.c / bsp_beep.h
bsp_rs485.c / bsp_rs485.h
bsp_adc.c / bsp_adc.h
bsp_tft.c / bsp_tft.h
bsp_touch.c / bsp_touch.h
bsp_w25q64.c / bsp_w25q64.h
bsp_flash.c / bsp_flash.h
bsp_esp32.c / bsp_esp32.h
bsp_backlight.c / bsp_backlight.h
```

BSP 层只做硬件操作，不写业务判断。

例如：

```c
void BSP_LED_On(uint8_t id);
void BSP_LED_Off(uint8_t id);
void BSP_LED_Toggle(uint8_t id);

void BSP_Beep_On(void);
void BSP_Beep_Off(void);

void BSP_RS485_SetTx(uint8_t port);
void BSP_RS485_SetRx(uint8_t port);
```

------

## 8.2 协议层

```text
protocol_modbus.c / protocol_modbus.h
protocol_upload.c / protocol_upload.h
```

Modbus 层负责：

- CRC16
- 组帧
- 解帧
- 寄存器解析
- 异常码判断

Upload 层负责：

- 文本数据封装
- JSON 数据封装
- 上位机命令解析，后续实现

------

## 8.3 应用层

```text
app_sensor.c / app_sensor.h
app_display.c / app_display.h
app_key.c / app_key.h
app_touch.c / app_touch.h
app_alarm.c / app_alarm.h
app_config.c / app_config.h
app_upload.c / app_upload.h
app_backlight.c / app_backlight.h
app_monitor.c / app_monitor.h
app_main.c / app_main.h
```

应用层负责具体业务逻辑。

------

# 9. 推荐目录结构

```text
Core/
├── Inc/
│   ├── app_main.h
│   ├── app_sensor.h
│   ├── app_display.h
│   ├── app_key.h
│   ├── app_touch.h
│   ├── app_alarm.h
│   ├── app_config.h
│   ├── app_upload.h
│   ├── app_backlight.h
│   ├── app_monitor.h
│   ├── bsp_led.h
│   ├── bsp_key.h
│   ├── bsp_beep.h
│   ├── bsp_rs485.h
│   ├── bsp_adc.h
│   ├── bsp_tft.h
│   ├── bsp_touch.h
│   ├── bsp_w25q64.h
│   ├── bsp_flash.h
│   ├── bsp_esp32.h
│   ├── protocol_modbus.h
│   └── protocol_upload.h
│
├── Src/
│   ├── app_main.c
│   ├── app_sensor.c
│   ├── app_display.c
│   ├── app_key.c
│   ├── app_touch.c
│   ├── app_alarm.c
│   ├── app_config.c
│   ├── app_upload.c
│   ├── app_backlight.c
│   ├── app_monitor.c
│   ├── bsp_led.c
│   ├── bsp_key.c
│   ├── bsp_beep.c
│   ├── bsp_rs485.c
│   ├── bsp_adc.c
│   ├── bsp_tft.c
│   ├── bsp_touch.c
│   ├── bsp_w25q64.c
│   ├── bsp_flash.c
│   ├── bsp_esp32.c
│   ├── protocol_modbus.c
│   └── protocol_upload.c
```

如果当前工程是 STM32CubeMX 生成的工程，请尽量不要大幅修改自动生成代码区域。
用户代码应该放到 `USER CODE BEGIN` 和 `USER CODE END` 之间，或者放到独立 app / bsp 文件中。

------

# 10. Claude Code 代码生成要求

请按照以下原则生成代码：

## 10.1 不要一次性生成全部功能

请优先生成基础框架，然后逐步补充模块。

建议顺序：

```text
1. BSP 基础驱动框架
2. FreeRTOS 任务框架
3. LED / 蜂鸣器 / 按键
4. USART1 日志输出
5. RS485 + Modbus 基础通信
6. ADC 采集模块
7. 告警模块
8. 配置保存模块
9. LVGL 显示框架
10. 背光管理
11. ESP32 预留接口
```

------

## 10.2 代码风格要求

- 使用 C 语言
- 使用 HAL 库
- 使用 FreeRTOS API
- 尽量避免复杂宏魔法
- 函数命名清晰
- 每个模块有 `.c` 和 `.h`
- 重要函数加注释
- 不要把所有逻辑放在 main.c
- 不要直接在中断中做复杂处理
- 中断中只置标志或发送轻量事件
- 共享数据需要考虑互斥访问

------

## 10.3 串口接收建议

RS485 Modbus 接收建议优先使用：

```text
UART DMA + IDLE 空闲中断
```

如果当前阶段为了简单，也可以先使用阻塞式接收，但需要封装好，后续方便替换。

------

## 10.4 LVGL 注意事项

LVGL 需要：

```text
周期性调用 lv_timer_handler()
提供 tick
提供显示 flush 回调
提供触摸 indev 回调
```

当前阶段如果 TFT 驱动没有完全写好，可以先生成 LVGL 页面框架和数据更新接口。

------

# 11. 分阶段实现目标

## 第一阶段：基础外设验证

目标：

```text
LED 可闪烁
蜂鸣器可响
按键可识别
USART1 可打印日志
ADC 可读取原始值
背光可开关
```

完成后串口输出示例：

```text
[SYS] Board init ok
[KEY] KEY1 short press
[ADC] VIN=12.05V, AIN1=1.23V
```

------

## 第二阶段：RS485 Modbus 通信

目标：

```text
能通过 RS485_1 读取一个 Modbus 传感器
能校验 CRC
能判断超时
能解析寄存器
```

输出示例：

```text
[MODBUS] TX addr=1 func=3 reg=0x0000 len=2
[MODBUS] RX ok
[DATA] TEMP=26.5,HUMI=63.2
```

------

## 第三阶段：多传感器轮询

目标：

```text
轮询温湿度、人体存在、光照、CO2
维护各传感器在线状态
统计 timeout 和 CRC error
```

输出示例：

```text
[DATA] TEMP=26.5,HUMI=63.2,CO2=680,LIGHT=350,HUMAN=1
[COMM] TH=OK,HUMAN=OK,LIGHT=OK,CO2=TIMEOUT
```

------

## 第四阶段：告警和参数

目标：

```text
阈值判断
蜂鸣器告警
LED 告警
按键静音
参数保存
恢复默认参数
```

输出示例：

```text
[ALARM] CO2 HIGH value=1500 threshold=1000
[CONFIG] save ok
```

------

## 第五阶段：LVGL 显示

目标：

```text
主页显示实时数据
通信页显示在线状态
告警页显示告警源
参数页支持设置
ADC 页显示模拟量
系统页显示运行信息
```

------

## 第六阶段：ESP32 / 上位机扩展

目标：

```text
UART4 与 ESP32 通信
USART1 支持 JSON 上传
后续 C# WPF 上位机解析显示
```

------

# 12. 当前优先实现内容

当前请优先帮我生成：

```text
1. 项目模块文件结构建议
2. BSP 层基础代码框架
3. FreeRTOS 任务创建框架
4. LED / 蜂鸣器 / 按键模块
5. ADC 采集模块
6. RS485 方向控制模块
7. Modbus CRC16 和基础组帧函数
8. USART1 日志输出接口
9. 全局数据结构定义
10. 后续 LVGL 页面数据接口预留
```

暂时不要直接实现复杂 LVGL 界面，也不要直接写死具体传感器寄存器。
传感器寄存器地址请用宏定义占位，后续我会根据实际传感器手册修改。

------

# 13. 重要提醒

## 13.1 SPI1 总线共享

SPI1 同时连接 TFT 和 W25Q64。

因此必须注意：

```text
操作 TFT：LCD_CS 拉低，W25Q64_CS 拉高
操作 W25Q64：W25Q64_CS 拉低，LCD_CS 拉高
空闲时：两个 CS 都拉高
```

------

## 13.2 背光与触摸独立

LCD_BL 只控制背光。

关闭背光后：

```text
TFT 控制器仍可保持工作
触摸芯片仍可保持工作
I2C1 仍可通信
CTP_INT 仍可触发
```

因此可以实现：

```text
无操作 30s -> 关闭背光
触摸或按键 -> 打开背光
```

------

## 13.3 ADC 输入保护

ADC 引脚不能超过 3.3V。

如果检测 12V 输入，需要外部分压电路。
软件中需要根据分压比例换算实际输入电压。

------

## 13.4 RS485 半双工控制

发送前：

```c
DIR = 1;
```

发送完成后：

```c
DIR = 0;
```

必须等待 UART 发送完成后再切换为接收模式。

------

## 13.5 Flash 写入保护

内部 Flash 不要频繁写。

建议：

```text
参数变化 -> 设置 dirty 标志
几秒后无新变化 -> 统一写入 Flash
```

------

# 14. 项目最终目标

最终系统应具备：

```text
RS485 多传感器采集
ADC 模拟量采集
TFT 本地显示
触摸 / 按键交互
背光自动休眠和唤醒
蜂鸣器 / LED 告警
参数掉电保存
通信异常检测
传感器离线判断
USART1 上位机数据上传
ESP32 网络扩展接口
FreeRTOS 多任务架构
```

