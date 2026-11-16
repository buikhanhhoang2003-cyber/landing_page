/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart Datalogger main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "sht30.h"
#include "bme280.h"
#include "sen0390.h"
#include "sm7901b.h"
#include "scheduler.h"
#include "datalogger.h"
#include "thermal_stream.h"
#include "stdio.h"
#include "stdint.h"
#include "string.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
typedef union
{
  uint8_t all;
  struct
  {
    uint8_t ch1 : 1;
    uint8_t ch2 : 1;
    uint8_t ch3 : 1;
    uint8_t ch4 : 1;
    uint8_t reserved : 4;
  } bit;
} IO4State_t;

typedef struct
{
  uint32_t menuStartTick;
  uint32_t lastUiTick;
  uint8_t currentMode;
  uint8_t previousMode;
} UIState_t;

typedef struct
{
  uint32_t lastReadTick;
  uint16_t mv[2];
} ADCState_t;

typedef struct
{
  uint32_t lastReadTick;
  IO4State_t state;
} DigitalInputState_t;

typedef struct
{
  uint32_t lastUpdateTick;
  int16_t temperature10;
  uint16_t humidity10;
  uint16_t pressure10;
  uint32_t lightLux;
  uint16_t noise10;
  uint8_t sht30Valid;
  uint8_t bme280Valid;
  uint8_t bme280Initialized;
  uint8_t sen0390Valid;
  uint8_t sm7901bValid;
} EnvironmentState_t;

typedef struct
{
  volatile uint32_t sample;
  uint8_t running;
} DACOutputState_t;

typedef struct
{
  uint32_t startTick;
  uint32_t delayMs;
  uint8_t active;
  uint8_t targetState;
} RelayTimerSlot_t;

typedef struct
{
  RelayTimerSlot_t relay[4];
} RelayTimerState_t;

typedef struct
{
  uint32_t lastLogTick;
  uint32_t lastUploadTriggerTick;
  uint32_t lastUploadTxTick;
  uint32_t inFlightSequence;
  uint32_t retryCount;
  uint8_t uploadDue;
  uint8_t waitingAck;
  uint8_t lastEvents;
} DataLoggerRuntime_t;

typedef struct
{
  char webIp[16];
  uint8_t valid;
} Esp32NetworkState_t;

typedef struct
{
  UIState_t ui;
  ADCState_t adc;
  DigitalInputState_t digitalInput;
  DACOutputState_t dac;
  RelayTimerState_t relayTimer;
  EnvironmentState_t environment;
  DataLoggerRuntime_t dataLogger;
  Esp32NetworkState_t network;
} AppState_t;

typedef struct
{
  uint8_t rxByte;
  volatile uint16_t rxHead;
  volatile uint16_t rxTail;
  volatile uint8_t rxOverflow;
  uint8_t rxRing[256];
  uint16_t rxLength;
  uint8_t discardLine;
  uint32_t lastTxTick;
  char rxLine[128];
  char txBuffer[192];
} Esp32Link_t;

typedef enum
{
  MLX_ERR_NONE = 0,
  MLX_ERR_I2C_NOT_READY,
  MLX_ERR_DUMP_EE,
  MLX_ERR_EXTRACT_PARAMS,
  MLX_ERR_SET_REFRESH,
  MLX_ERR_SET_MODE,
  MLX_ERR_FRAME
} ThermalError_t;

typedef struct
{
  paramsMLX90640 params;
  uint16_t frameData[834];
  float temperature[768];
  uint32_t lastFrameTick;
  uint32_t lastInitTick;
  float minTemp;
  float maxTemp;
  float centerTemp;
  float ambientTemp;
  int16_t lastError;
  ThermalError_t error;
  uint16_t frameSequence;
  uint8_t frameErrorCount;
  uint8_t subpageMask;
  uint8_t initialized;
  uint8_t frameReady;
} ThermalState_t;
/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define UI_BG                              BLACK
#define UI_HEADER_BG                       0x7800
#define UI_CARD_BG                         0x2128
#define UI_CARD_BORDER                     0xA3A0
#define UI_TITLE_COLOR                     0xFE00
#define UI_LABEL_COLOR                     0x07FF
#define UI_VALUE_COLOR                     WHITE
#define UI_RELAY_ON                        0xFE00
#define UI_RELAY_OFF                       0x52AA
#define DIGITAL_READ_PERIOD_MS             10U
#define ADC_READ_PERIOD_MS                 20U
#define UI_REFRESH_PERIOD_MS               250U
#define ADC_VREF_MV                        3300U
#define ADC_MAX_VALUE                      4095U
#define MLX90640_ADDRESS                   0x33U
#define MLX90640_EMISSIVITY                0.95f
#define MLX90640_TA_SHIFT                  8.0f
#define THERMAL_FRAME_PERIOD_MS            250U
#define THERMAL_RETRY_PERIOD_MS            1000U
#define THERMAL_IMAGE_X                    32U
#define THERMAL_IMAGE_Y                    40U
#define THERMAL_PIXEL_WIDTH                8U
#define THERMAL_PIXEL_HEIGHT               6U
#define MODE_MENU                          0U
#define MODE_SENSOR                        1U
#define MODE_THERMAL                       2U
#define MODE_OUTPUT                        3U
#define MODE_TIMER                         4U
#define ESP32_SENSOR_TX_PERIOD_MS          200U
#define ESP32_UART_TX_TIMEOUT_MS           20U
#define ENV_SENSOR_UPDATE_PERIOD_MS        1000U
#define DATALOGGER_LOG_INTERVAL_MS         5000U
#define DATALOGGER_UPLOAD_INTERVAL_MS      10000U
#define DATALOGGER_ACK_TIMEOUT_MS          3000U
#define DATALOGGER_MAX_INTERVAL_SEC        86400UL
#define RELAY_TIMER_MAX_SECONDS            86400UL
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
CAN_HandleTypeDef hcan;
DAC_HandleTypeDef hdac;
DMA_HandleTypeDef hdma_dac_ch1;
DMA_HandleTypeDef hdma_dac_ch2;
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_tx;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
USART_HandleTypeDef husart3;

/* USER CODE BEGIN PV */
static AppState_t app = {0};
static ThermalState_t thermal = {0};
static Esp32Link_t esp32Link = {0};
static SHT30_Handle_t sht30 = {0};
static SEN0390_Handle_t sen0390 = {0};
static SM7901B_Handle_t sm7901b = {0};
static Scheduler_t scheduler = {0};
static DataLogger_t logBuffer = {0};
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_CAN_Init(void);
static void MX_DAC_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */
static void UI_Init(void);
static void UI_DrawModeMenu(void);
static void UI_DrawDashboard(void);
static void UI_DrawActiveControl(void);
static void UI_DrawThermalCamera(void);
static void UI_DrawRelayTimer(void);
static void UI_UpdateRelayTimer(uint32_t now);
static void UI_DrawWebIp(void);
static void UI_DrawCurrentMode(void);
static void UI_DrawSensorCard(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *title, const char *value, const char *unit);
static void UI_DrawRelay(uint16_t x, uint16_t y, const char *name, uint8_t state);
static void UI_FormatFixed1(int32_t value10, char *buffer, uint16_t bufferSize);
static void DataLogger_Process(uint32_t now);
static void DataLogger_CaptureRecord(uint32_t now);
static void DataLogger_ServiceUpload(uint32_t now);
static void DataLogger_HandleAck(uint32_t sequence);
static uint8_t ESP32_SendLogRecord(const DataLogRecord_t *record, uint32_t now);
static HAL_StatusTypeDef BME280_AppInit(void);
static void Sensors_Init(void);
static void Sensors_Update(uint32_t now);
static int Thermal_Init(void);
static void Thermal_Update(uint32_t now);
static void Thermal_DrawFrame(void);
static void Thermal_UpdateStatistics(void);
static void Thermal_FormatTemperature(float temperature, char *buffer, uint16_t bufferSize);
static uint16_t Thermal_GetColor(float temperature, float minTemperature, float maxTemperature);
static void ESP32_ProcessCommands(uint32_t now);
static void ESP32_HandleCommand(const char *line, uint32_t now);
static void ESP32_SendText(const char *text);
static void ESP32_SendSchedulerStatus(void);
static void ESP32_SendIoStatus(void);
static void ESP32_SendSensorStatus(void);
static void ESP32_SendThermalStatus(void);
static void ESP32_SendModeStatus(void);
static void ESP32_SendTimerStatus(uint32_t now);
static void ESP32_SendSensorData(uint32_t now);
static uint8_t Output_GetRelayMask(void);
static HAL_StatusTypeDef Output_SetRelay(uint8_t relayIndex, GPIO_PinState state);
static void Output_SetAllRelays(GPIO_PinState state);
static HAL_StatusTypeDef Output_SetDac1(uint16_t code);
static HAL_StatusTypeDef Output_StopDac1(void);
static void DigitalInput_Update(uint32_t now);
static void ADC_Update(uint32_t now);
static void RelayTimer_Process(uint32_t now);
static void RelayTimer_CancelAll(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
static void ESP32_SendText(const char *text)
{
  size_t length;

  if (text == NULL) return;
  length = strlen(text);
  if ((length == 0U) || (length > 0xFFFFU)) return;
  (void)HAL_UART_Transmit(&huart2, (uint8_t *)text, (uint16_t)length, ESP32_UART_TX_TIMEOUT_MS);
}

static void ESP32_SendSchedulerStatus(void)
{
  int len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "STATUS,SCHED,%u,%lu,%lu,%u,%lu,%lu,%lu,%u,%lu\r\n", (unsigned int)scheduler.enabled, (unsigned long)(scheduler.logIntervalMs / 1000U), (unsigned long)(scheduler.uploadIntervalMs / 1000U), (unsigned int)DataLogger_Count(&logBuffer), (unsigned long)logBuffer.totalLogged, (unsigned long)logBuffer.totalAcked, (unsigned long)logBuffer.dropped, (unsigned int)app.dataLogger.waitingAck, (unsigned long)app.dataLogger.inFlightSequence);
  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static uint8_t Output_GetRelayMask(void)
{
  uint8_t mask = 0U;
  if (HAL_GPIO_ReadPin(GPIOC, Y1_OUTPUT_Pin) == GPIO_PIN_SET) mask |= (1U << 0);
  if (HAL_GPIO_ReadPin(GPIOC, Y2_OUTPUT_Pin) == GPIO_PIN_SET) mask |= (1U << 1);
  if (HAL_GPIO_ReadPin(GPIOB, Y3_OUTPUT_Pin) == GPIO_PIN_SET) mask |= (1U << 2);
  if (HAL_GPIO_ReadPin(GPIOB, Y4_OUTPUT_Pin) == GPIO_PIN_SET) mask |= (1U << 3);
  return mask;
}

static HAL_StatusTypeDef Output_SetRelay(uint8_t relayIndex, GPIO_PinState state)
{
  switch (relayIndex)
  {
    case 1U: HAL_GPIO_WritePin(GPIOC, Y1_OUTPUT_Pin, state); return HAL_OK;
    case 2U: HAL_GPIO_WritePin(GPIOC, Y2_OUTPUT_Pin, state); return HAL_OK;
    case 3U: HAL_GPIO_WritePin(GPIOB, Y3_OUTPUT_Pin, state); return HAL_OK;
    case 4U: HAL_GPIO_WritePin(GPIOB, Y4_OUTPUT_Pin, state); return HAL_OK;
    default: return HAL_ERROR;
  }
}

static void Output_SetAllRelays(GPIO_PinState state)
{
  HAL_GPIO_WritePin(GPIOC, Y1_OUTPUT_Pin | Y2_OUTPUT_Pin, state);
  HAL_GPIO_WritePin(GPIOB, Y3_OUTPUT_Pin | Y4_OUTPUT_Pin, state);
}

static HAL_StatusTypeDef Output_SetDac1(uint16_t code)
{
  HAL_StatusTypeDef status;
  if (code > ADC_MAX_VALUE) return HAL_ERROR;

  if (app.dac.running == 0U)
  {
    status = HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    if (status != HAL_OK) return status;
    app.dac.running = 1U;
  }

  status = HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code);
  if (status == HAL_OK) app.dac.sample = code;
  return status;
}

static HAL_StatusTypeDef Output_StopDac1(void)
{
  HAL_StatusTypeDef status = HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0U);
  if (status != HAL_OK) return status;

  if (app.dac.running != 0U)
  {
    status = HAL_DAC_Stop(&hdac, DAC_CHANNEL_1);
    if (status != HAL_OK) return status;
  }

  app.dac.sample = 0U;
  app.dac.running = 0U;
  return HAL_OK;
}

static void ESP32_SendModeStatus(void)
{
  int len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "STATUS,MODE,%u\r\n", (unsigned int)app.ui.currentMode);
  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static void ESP32_SendIoStatus(void)
{
  uint8_t relayMask = Output_GetRelayMask();
  uint8_t diMask = (uint8_t)(app.digitalInput.state.all & 0x0FU);
  uint32_t dacMv = (app.dac.sample * ADC_VREF_MV) / ADC_MAX_VALUE;
  int len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "STATUS,IO,%u,%lu,%lu,%u,%u,%u,%u\r\n", (unsigned int)relayMask, (unsigned long)app.dac.sample, (unsigned long)dacMv, (unsigned int)app.dac.running, (unsigned int)diMask, (unsigned int)app.adc.mv[0], (unsigned int)app.adc.mv[1]);
  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static void ESP32_SendTimerStatus(uint32_t now)
{
  uint8_t relayIndex;
  for (relayIndex = 0U; relayIndex < 4U; relayIndex++)
  {
    RelayTimerSlot_t *slot = &app.relayTimer.relay[relayIndex];
    uint32_t remainingSeconds = 0U;

    if (slot->active != 0U)
    {
      uint32_t elapsed = now - slot->startTick;
      if (elapsed < slot->delayMs) remainingSeconds = (slot->delayMs - elapsed + 999U) / 1000U;
    }

    (void)snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "STATUS,TIMER,%u,%u,%lu,%s\r\n", (unsigned int)(relayIndex + 1U), (unsigned int)slot->active, (unsigned long)remainingSeconds, slot->targetState ? "ON" : "OFF");
    ESP32_SendText(esp32Link.txBuffer);
  }
}

static void RelayTimer_CancelAll(void)
{
  uint8_t relayIndex;
  for (relayIndex = 0U; relayIndex < 4U; relayIndex++) app.relayTimer.relay[relayIndex].active = 0U;
}

static void RelayTimer_Process(uint32_t now)
{
  uint8_t relayIndex;

  for (relayIndex = 0U; relayIndex < 4U; relayIndex++)
  {
    RelayTimerSlot_t *slot = &app.relayTimer.relay[relayIndex];

    if ((slot->active != 0U) && ((uint32_t)(now - slot->startTick) >= slot->delayMs))
    {
      (void)Output_SetRelay((uint8_t)(relayIndex + 1U), slot->targetState ? GPIO_PIN_SET : GPIO_PIN_RESET);
      slot->active = 0U;
      (void)snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "EVENT,TIMER,RELAY,%u,%s\r\n", (unsigned int)(relayIndex + 1U), slot->targetState ? "ON" : "OFF");
      ESP32_SendText(esp32Link.txBuffer);
    }
  }
}

static void ESP32_SendSensorStatus(void)
{
  uint8_t validMask = 0U;
  int len;

  if (app.environment.sht30Valid != 0U) validMask |= DATALOGGER_VALID_SHT30;
  if (app.environment.bme280Valid != 0U) validMask |= DATALOGGER_VALID_BME280;
  if (app.environment.sen0390Valid != 0U) validMask |= DATALOGGER_VALID_SEN0390;
  if (app.environment.sm7901bValid != 0U) validMask |= DATALOGGER_VALID_SM7901B;

  len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "STATUS,SENSOR,%d,%u,%u,%lu,%u,%u\r\n", (int)app.environment.temperature10, (unsigned int)app.environment.humidity10, (unsigned int)app.environment.pressure10, (unsigned long)app.environment.lightLux, (unsigned int)app.environment.noise10, (unsigned int)validMask);
  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static void ESP32_SendThermalStatus(void)
{
  int len;
  int16_t ambient10 = ThermalStream_EncodeTemperature10(thermal.ambientTemp);
  int16_t center10 = ThermalStream_EncodeTemperature10(thermal.centerTemp);
  int16_t min10 = ThermalStream_EncodeTemperature10(thermal.minTemp);
  int16_t max10 = ThermalStream_EncodeTemperature10(thermal.maxTemp);

  len = snprintf(
    esp32Link.txBuffer,
    sizeof(esp32Link.txBuffer),
    "STATUS,THERMAL,%u,%u,%d,%d,%d,%d,%d\r\n",
    (unsigned int)thermal.frameReady,
    (unsigned int)thermal.frameSequence,
    (int)ambient10,
    (int)center10,
    (int)min10,
    (int)max10,
    (int)thermal.lastError);

  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static void DataLogger_HandleAck(uint32_t sequence)
{
  int len;

  if (DataLogger_AckOldest(&logBuffer, sequence) == 0U)
  {
    len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "ERR,ACK,%lu\r\n", (unsigned long)sequence);
    if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
    return;
  }

  app.dataLogger.waitingAck = 0U;
  app.dataLogger.inFlightSequence = 0U;
  app.dataLogger.retryCount = 0U;
  if (DataLogger_Count(&logBuffer) == 0U) app.dataLogger.uploadDue = 0U;

  len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,ACK,%lu\r\n", (unsigned long)sequence);
  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
}

static void UI_DrawWebIp(void)
{
  char line[24];
  ILI9341_DrawRectangle(0, 204, 320, 36, UI_BG);

  if (app.network.valid != 0U)
  {
    (void)snprintf(line, sizeof(line), "WEB: %s", app.network.webIp);
    ILI9341_DrawText(line, FONT1, 78, 215, CYAN, UI_BG);
  }
  else
  {
    ILI9341_DrawText("WAITING WEB IP ........", FONT1, 94, 215, LIGHTGREY, UI_BG);
  }
}

static void ESP32_HandleCommand(const char *line, uint32_t now)
{
  unsigned int mode;
  unsigned long value;
  unsigned long sequence;
  int len;

  if (line == NULL) return;

  /* WEBIP is additive only: it never changes the active mode. */
  {
    unsigned int ip0;
    unsigned int ip1;
    unsigned int ip2;
    unsigned int ip3;
    char extra;

    if (sscanf(line, "WEBIP,%u.%u.%u.%u%c", &ip0, &ip1, &ip2, &ip3, &extra) == 4)
    {
      char newIp[16];
      uint8_t changed;

      if ((ip0 > 255U) || (ip1 > 255U) || (ip2 > 255U) || (ip3 > 255U)) return;

      (void)snprintf(newIp, sizeof(newIp), "%u.%u.%u.%u", ip0, ip1, ip2, ip3);
      changed = (app.network.valid == 0U) || (strcmp(app.network.webIp, newIp) != 0);

      if (changed != 0U)
      {
        (void)snprintf(app.network.webIp, sizeof(app.network.webIp), "%s", newIp);
        app.network.valid = 1U;
        if (app.ui.currentMode == MODE_MENU) UI_DrawWebIp();
      }
      else
      {
        app.network.valid = 1U;
      }

      return;
    }
  }

  if (sscanf(line, "MODE,%u", &mode) == 1)
  {
    if (mode <= MODE_TIMER)
    {
      app.ui.currentMode = (uint8_t)mode;
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,MODE,%u\r\n", mode);
    }
    else
    {
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "ERR,MODE\r\n");
    }

    if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
    return;
  }

  {
    unsigned int relayIndex;
    char relayState[8];

    if (sscanf(line, "RELAY,%u,%7s", &relayIndex, relayState) == 2)
    {
      GPIO_PinState pinState;

      if (strcmp(relayState, "ON") == 0) pinState = GPIO_PIN_SET;
      else if (strcmp(relayState, "OFF") == 0) pinState = GPIO_PIN_RESET;
      else
      {
        ESP32_SendText("ERR,RELAY\r\n");
        return;
      }

      if (Output_SetRelay((uint8_t)relayIndex, pinState) != HAL_OK)
      {
        ESP32_SendText("ERR,RELAY\r\n");
        return;
      }

      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,RELAY,%u,%s\r\n", relayIndex, relayState);
      if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
      return;
    }
  }

  if (strcmp(line, "RELAY,ALL,ON") == 0)
  {
    Output_SetAllRelays(GPIO_PIN_SET);
    ESP32_SendText("OK,RELAY,ALL,ON\r\n");
    return;
  }

  if (strcmp(line, "RELAY,ALL,OFF") == 0)
  {
    Output_SetAllRelays(GPIO_PIN_RESET);
    ESP32_SendText("OK,RELAY,ALL,OFF\r\n");
    return;
  }

  if (strcmp(line, "DAC,1,OFF") == 0)
  {
    ESP32_SendText((Output_StopDac1() == HAL_OK) ? "OK,DAC,1,OFF\r\n" : "ERR,DAC\r\n");
    return;
  }

  {
    unsigned int relayIndex;
    unsigned long delaySeconds;
    char targetState[4];

    if (sscanf(line, "TIMER RELAY %u %lus %3s", &relayIndex, &delaySeconds, targetState) == 3)
    {
      if ((relayIndex < 1U) || (relayIndex > 4U))
      {
        ESP32_SendText("ERR,TIMER\r\n");
        return;
      }

      if ((delaySeconds == 0UL) || (delaySeconds > RELAY_TIMER_MAX_SECONDS))
      {
        ESP32_SendText("ERR,TIME\r\n");
        return;
      }

      if ((strcmp(targetState, "ON") != 0) && (strcmp(targetState, "OFF") != 0))
      {
        ESP32_SendText("ERR,TIMER\r\n");
        return;
      }

      app.relayTimer.relay[relayIndex - 1U].startTick = now;
      app.relayTimer.relay[relayIndex - 1U].delayMs = (uint32_t)(delaySeconds * 1000UL);
      app.relayTimer.relay[relayIndex - 1U].targetState = (strcmp(targetState, "ON") == 0) ? 1U : 0U;
      app.relayTimer.relay[relayIndex - 1U].active = 1U;

      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,TIMER,RELAY,%u,%lu,%s\r\n", relayIndex, delaySeconds, targetState);
      if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
      return;
    }
  }

  if (strcmp(line, "RESET") == 0)
  {
    RelayTimer_CancelAll();
    ESP32_SendText("OK,RESET,TIMER\r\n");
    return;
  }

  {
    unsigned int dacChannel;
    unsigned long dacCode;

    if (sscanf(line, "DAC,%u,%lu", &dacChannel, &dacCode) == 2)
    {
      if ((dacChannel != 1U) || (dacCode > ADC_MAX_VALUE))
      {
        ESP32_SendText("ERR,DAC\r\n");
        return;
      }

      if (Output_SetDac1((uint16_t)dacCode) != HAL_OK)
      {
        ESP32_SendText("ERR,DAC\r\n");
        return;
      }

      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,DAC,1,%lu\r\n", dacCode);
      if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
      return;
    }
  }

  if (strcmp(line, "STATUS,MODE") == 0) { ESP32_SendModeStatus(); return; }
  if (strcmp(line, "STATUS,IO") == 0) { ESP32_SendIoStatus(); return; }
  if (strcmp(line, "STATUS,SENSOR") == 0) { ESP32_SendSensorStatus(); return; }
  if (strcmp(line, "STATUS,THERMAL") == 0) { ESP32_SendThermalStatus(); return; }
  if (strcmp(line, "STATUS,SCHED") == 0) { ESP32_SendSchedulerStatus(); return; }
  if (strcmp(line, "STATUS,TIMER") == 0) { ESP32_SendTimerStatus(now); return; }

  if (strcmp(line, "STATUS,ALL") == 0)
  {
    ESP32_SendModeStatus();
    ESP32_SendIoStatus();
    ESP32_SendSensorStatus();
    ESP32_SendThermalStatus();
    ESP32_SendSchedulerStatus();
    ESP32_SendTimerStatus(now);
    return;
  }

  if (strcmp(line, "THERMAL,FRAME") == 0)
  {
    if (app.ui.currentMode != MODE_THERMAL)
    {
      ESP32_SendText("ERR,THERMAL,MODE\r\n");
      return;
    }

    if ((thermal.initialized == 0U) || (thermal.frameReady == 0U))
    {
      ESP32_SendText("ERR,THERMAL,NOT_READY\r\n");
      return;
    }

    (void)ThermalStream_SendFrame(
      &huart2,
      thermal.temperature,
      thermal.frameSequence,
      thermal.ambientTemp,
      thermal.centerTemp,
      thermal.minTemp,
      thermal.maxTemp);

    return;
  }

  if (sscanf(line, "SCHED,LOG,%lu", &value) == 1)
  {
    if ((value >= 1UL) && (value <= DATALOGGER_MAX_INTERVAL_SEC))
    {
      Scheduler_SetIntervals(&scheduler, (uint32_t)(value * 1000UL), scheduler.uploadIntervalMs);
      scheduler.lastLogTick = now;
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,SCHED,LOG,%lu\r\n", value);
    }
    else
    {
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "ERR,SCHED,LOG\r\n");
    }

    if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
    return;
  }

  if (sscanf(line, "SCHED,UPLOAD,%lu", &value) == 1)
  {
    if ((value >= 1UL) && (value <= DATALOGGER_MAX_INTERVAL_SEC))
    {
      Scheduler_SetIntervals(&scheduler, scheduler.logIntervalMs, (uint32_t)(value * 1000UL));
      scheduler.lastUploadTick = now;
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "OK,SCHED,UPLOAD,%lu\r\n", value);
    }
    else
    {
      len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "ERR,SCHED,UPLOAD\r\n");
    }

    if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) ESP32_SendText(esp32Link.txBuffer);
    return;
  }

  if (strcmp(line, "SCHED,START") == 0)
  {
    scheduler.lastLogTick = now;
    scheduler.lastUploadTick = now;
    Scheduler_Enable(&scheduler, 1U);
    ESP32_SendText("OK,SCHED,START\r\n");
    return;
  }

  if (strcmp(line, "SCHED,STOP") == 0)
  {
    Scheduler_Enable(&scheduler, 0U);
    app.dataLogger.uploadDue = 0U;
    app.dataLogger.waitingAck = 0U;
    app.dataLogger.inFlightSequence = 0U;
    app.dataLogger.retryCount = 0U;
    ESP32_SendText("OK,SCHED,STOP\r\n");
    return;
  }

  if (strcmp(line, "SCHED,STATUS") == 0)
  {
    ESP32_SendSchedulerStatus();
    return;
  }

  if (sscanf(line, "ACK,%lu", &sequence) == 1)
  {
    DataLogger_HandleAck((uint32_t)sequence);
    return;
  }

  ESP32_SendText("ERR,CMD\r\n");
}

static void ESP32_ProcessCommands(uint32_t now)
{
  while (esp32Link.rxTail != esp32Link.rxHead)
  {
    uint8_t rxByte = esp32Link.rxRing[esp32Link.rxTail];
    esp32Link.rxTail = (uint16_t)((esp32Link.rxTail + 1U) & 0x00FFU);

    if ((esp32Link.rxLength == 0U) && (rxByte >= '0') && (rxByte <= '4'))
    {
      app.ui.currentMode = (uint8_t)(rxByte - '0');
      continue;
    }

    if (rxByte == '\r') continue;

    if (rxByte == '\n')
    {
      if (esp32Link.discardLine != 0U)
      {
        esp32Link.discardLine = 0U;
        esp32Link.rxLength = 0U;
        ESP32_SendText("ERR,LINE\r\n");
        continue;
      }

      if (esp32Link.rxLength > 0U)
      {
        esp32Link.rxLine[esp32Link.rxLength] = '\0';
        ESP32_HandleCommand(esp32Link.rxLine, now);
        esp32Link.rxLength = 0U;
      }

      continue;
    }

    if (esp32Link.rxLength < (sizeof(esp32Link.rxLine) - 1U)) esp32Link.rxLine[esp32Link.rxLength++] = (char)rxByte;
    else esp32Link.discardLine = 1U;
  }

  if (esp32Link.rxOverflow != 0U)
  {
    esp32Link.rxOverflow = 0U;
    esp32Link.rxLength = 0U;
    esp32Link.discardLine = 1U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    uint16_t nextHead = (uint16_t)((esp32Link.rxHead + 1U) & 0x00FFU);

    if (nextHead != esp32Link.rxTail)
    {
      esp32Link.rxRing[esp32Link.rxHead] = esp32Link.rxByte;
      esp32Link.rxHead = nextHead;
    }
    else
    {
      esp32Link.rxOverflow = 1U;
    }

    (void)HAL_UART_Receive_IT(&huart2, &esp32Link.rxByte, 1U);
  }
}

static void DataLogger_CaptureRecord(uint32_t now)
{
  DataLogRecord_t record = {0};

  record.tickMs = now;
  record.temperature10 = app.environment.temperature10;
  record.humidity10 = app.environment.humidity10;
  record.pressure10 = app.environment.pressure10;
  record.lightLux = app.environment.lightLux;
  record.noise10 = app.environment.noise10;
  record.adc1Mv = app.adc.mv[0];
  record.adc2Mv = app.adc.mv[1];
  record.digitalInputs = (uint8_t)(app.digitalInput.state.all & 0x0FU);

  if (app.environment.sht30Valid != 0U) record.validMask |= DATALOGGER_VALID_SHT30;
  if (app.environment.bme280Valid != 0U) record.validMask |= DATALOGGER_VALID_BME280;
  if (app.environment.sen0390Valid != 0U) record.validMask |= DATALOGGER_VALID_SEN0390;
  if (app.environment.sm7901bValid != 0U) record.validMask |= DATALOGGER_VALID_SM7901B;

  (void)DataLogger_Push(&logBuffer, &record);
}

static uint8_t ESP32_SendLogRecord(const DataLogRecord_t *record, uint32_t now)
{
  int len;

  if (record == NULL) return 0U;

  len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "LOG,%lu,%lu,%d,%u,%u,%lu,%u,%u,%u,%u,%u\r\n", (unsigned long)record->sequence, (unsigned long)record->tickMs, (int)record->temperature10, (unsigned int)record->humidity10, (unsigned int)record->pressure10, (unsigned long)record->lightLux, (unsigned int)record->noise10, (unsigned int)record->adc1Mv, (unsigned int)record->adc2Mv, (unsigned int)record->digitalInputs, (unsigned int)record->validMask);

  if ((len <= 0) || ((size_t)len >= sizeof(esp32Link.txBuffer))) return 0U;
  if (HAL_UART_Transmit(&huart2, (uint8_t *)esp32Link.txBuffer, (uint16_t)len, ESP32_UART_TX_TIMEOUT_MS) != HAL_OK) return 0U;

  app.dataLogger.waitingAck = 1U;
  app.dataLogger.inFlightSequence = record->sequence;
  app.dataLogger.lastUploadTxTick = now;
  return 1U;
}

static void DataLogger_ServiceUpload(uint32_t now)
{
  const DataLogRecord_t *record;

  if (app.dataLogger.uploadDue == 0U) return;

  record = DataLogger_Peek(&logBuffer);
  if (record == NULL)
  {
    app.dataLogger.uploadDue = 0U;
    app.dataLogger.waitingAck = 0U;
    app.dataLogger.inFlightSequence = 0U;
    return;
  }

  if (app.dataLogger.waitingAck != 0U)
  {
    if ((now - app.dataLogger.lastUploadTxTick) < DATALOGGER_ACK_TIMEOUT_MS) return;
    app.dataLogger.retryCount++;
  }

  (void)ESP32_SendLogRecord(record, now);
}

static void DataLogger_Process(uint32_t now)
{
  uint8_t events = Scheduler_Update(&scheduler, now);
  app.dataLogger.lastEvents = events;

  if ((events & SCHEDULER_EVENT_LOG) != 0U)
  {
    DataLogger_CaptureRecord(now);
    app.dataLogger.lastLogTick = now;
  }

  if ((events & SCHEDULER_EVENT_UPLOAD) != 0U)
  {
    app.dataLogger.uploadDue = 1U;
    app.dataLogger.lastUploadTriggerTick = now;
  }

  DataLogger_ServiceUpload(now);
}

static void ESP32_SendSensorData(uint32_t now)
{
  int len;

  if ((now - esp32Link.lastTxTick) < ESP32_SENSOR_TX_PERIOD_MS) return;
  esp32Link.lastTxTick = now;

  len = snprintf(esp32Link.txBuffer, sizeof(esp32Link.txBuffer), "S,%u,%u,%u,%u,%u,%u,%u\r\n", (unsigned int)app.ui.currentMode, (unsigned int)app.adc.mv[0], (unsigned int)app.adc.mv[1], (unsigned int)app.digitalInput.state.bit.ch1, (unsigned int)app.digitalInput.state.bit.ch2, (unsigned int)app.digitalInput.state.bit.ch3, (unsigned int)app.digitalInput.state.bit.ch4);

  if ((len > 0) && ((size_t)len < sizeof(esp32Link.txBuffer))) (void)HAL_UART_Transmit(&huart2, (uint8_t *)esp32Link.txBuffer, (uint16_t)len, ESP32_UART_TX_TIMEOUT_MS);
}

static HAL_StatusTypeDef BME280_AppInit(void)
{
  uint32_t startTick;

  BME280_BindI2C(&hi2c2);
  if (BME280_Check() != BME280_SUCCESS) return HAL_ERROR;

  BME280_Reset();
  HAL_Delay(5U);

  startTick = HAL_GetTick();
  while ((BME280_ReadReg(BME280_REG_STATUS) & BME280_STATUS_IM_UPDATE) != 0U)
  {
    if ((HAL_GetTick() - startTick) > 20U) return HAL_TIMEOUT;
    HAL_Delay(1U);
  }

  if (BME280_Check() != BME280_SUCCESS) return HAL_ERROR;
  if (BME280_Read_Calibration() != BME280_SUCCESS) return HAL_ERROR;

  BME280_SetMode(BME280_MODE_SLEEP);
  BME280_SetOSRSH(BME280_OSRS_H_x1);
  BME280_SetOSRST(BME280_OSRS_T_x1);
  BME280_SetOSRSP(BME280_OSRS_P_x1);
  BME280_SetFilter(BME280_FILTER_OFF);
  BME280_SetStandby(BME280_STBY_1s);
  BME280_SetMode(BME280_MODE_NORMAL);
  return HAL_OK;
}

static void Sensors_Init(void)
{
  app.environment.lastUpdateTick = HAL_GetTick();
  app.environment.sht30Valid = 0U;
  app.environment.bme280Valid = 0U;
  app.environment.bme280Initialized = 0U;
  app.environment.sen0390Valid = 0U;
  app.environment.sm7901bValid = 0U;

  (void)SHT30_Init(&sht30, &hi2c1);
  if (BME280_AppInit() == HAL_OK) app.environment.bme280Initialized = 1U;
  (void)SEN0390_Init(&sen0390, &hi2c2);
  (void)SM7901B_Init(&sm7901b, &huart1, RS485_NOISE_DIR_GPIO_Port, RS485_NOISE_DIR_Pin);
}

static void Sensors_Update(uint32_t now)
{
  HAL_StatusTypeDef status;
  int16_t temperature10;
  uint16_t humidity10;
  int32_t bmeTemperature100;
  int32_t bmeRawTemperature;
  int32_t bmeRawPressure;
  int32_t bmeRawHumidity;
  uint32_t bmePressureQ24_8;
  uint32_t bmePressurePa;
  uint16_t pressure10;
  uint32_t lightLux;
  uint16_t noise10;

  if ((now - app.environment.lastUpdateTick) < ENV_SENSOR_UPDATE_PERIOD_MS) return;
  app.environment.lastUpdateTick = now;

  if (sht30.initialized == 0U)
  {
    app.environment.sht30Valid = 0U;
    (void)SHT30_Init(&sht30, &hi2c1);
  }

  if (sht30.initialized != 0U)
  {
    status = SHT30_Read(&sht30, &temperature10, &humidity10);

    if (status == HAL_OK)
    {
      app.environment.temperature10 = temperature10;
      app.environment.humidity10 = humidity10;
      app.environment.sht30Valid = 1U;
    }
    else
    {
      app.environment.sht30Valid = 0U;
      sht30.initialized = 0U;
    }
  }

  if (app.environment.bme280Initialized == 0U)
  {
    app.environment.bme280Valid = 0U;
    if (BME280_AppInit() == HAL_OK) app.environment.bme280Initialized = 1U;
  }

  if (app.environment.bme280Initialized != 0U)
  {
    if (BME280_Read_UTPH(&bmeRawTemperature, &bmeRawPressure, &bmeRawHumidity) == BME280_SUCCESS)
    {
      if ((bmeRawTemperature != 0x80000L) && (bmeRawPressure != 0x80000L))
      {
        bmeTemperature100 = BME280_CalcT(bmeRawTemperature);
        bmePressureQ24_8 = BME280_CalcP(bmeRawPressure);
        bmePressurePa = (bmePressureQ24_8 + 128U) >> 8;

        if ((bmeTemperature100 >= -4000L) && (bmeTemperature100 <= 8500L) && (bmePressurePa >= 30000U) && (bmePressurePa <= 110000U))
        {
          pressure10 = (uint16_t)((bmePressurePa + 5U) / 10U);
          app.environment.pressure10 = pressure10;
          app.environment.bme280Valid = 1U;
        }
        else
        {
          app.environment.bme280Valid = 0U;
        }
      }
      else
      {
        app.environment.bme280Valid = 0U;
      }
    }
    else
    {
      app.environment.bme280Valid = 0U;
      app.environment.bme280Initialized = 0U;
    }
  }

  if (sen0390.initialized == 0U)
  {
    app.environment.sen0390Valid = 0U;
    (void)SEN0390_Init(&sen0390, &hi2c2);
  }

  if (sen0390.initialized != 0U)
  {
    status = SEN0390_ReadLux(&sen0390, &lightLux);

    if (status == HAL_OK)
    {
      app.environment.lightLux = lightLux;
      app.environment.sen0390Valid = 1U;
    }
    else if (status != HAL_BUSY)
    {
      app.environment.sen0390Valid = 0U;
      sen0390.initialized = 0U;
    }
  }

  if (sm7901b.initialized == 0U)
  {
    app.environment.sm7901bValid = 0U;
    (void)SM7901B_Init(&sm7901b, &huart1, RS485_NOISE_DIR_GPIO_Port, RS485_NOISE_DIR_Pin);
  }

  if (sm7901b.initialized != 0U)
  {
    status = SM7901B_ReadNoise(&sm7901b, &noise10);

    if (status == HAL_OK)
    {
      app.environment.noise10 = noise10;
      app.environment.sm7901bValid = 1U;
    }
    else
    {
      app.environment.sm7901bValid = 0U;
    }
  }
}

static void UI_FormatFixed1(int32_t value10, char *buffer, uint16_t bufferSize)
{
  uint32_t absoluteValue = (value10 < 0) ? (uint32_t)(-value10) : (uint32_t)value10;
  (void)snprintf(buffer, bufferSize, "%s%lu.%lu", (value10 < 0) ? "-" : "", (unsigned long)(absoluteValue / 10U), (unsigned long)(absoluteValue % 10U));
}

static void UI_DrawSensorCard(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *title, const char *value, const char *unit)
{
  ILI9341_DrawRectangle(x, y, width, height, UI_CARD_BG);
  ILI9341_DrawHollowRectangleCoord(x, y, x + width - 1U, y + height - 1U, UI_CARD_BORDER);
  ILI9341_DrawText(title, FONT1, x + 6U, y + 5U, UI_LABEL_COLOR, UI_CARD_BG);
  ILI9341_DrawText(value, FONT2, x + 6U, y + 21U, UI_VALUE_COLOR, UI_CARD_BG);
  ILI9341_DrawText(unit, FONT1, x + width - 38U, y + 24U, LIGHTGREY, UI_CARD_BG);
}

static void UI_DrawRelay(uint16_t x, uint16_t y, const char *name, uint8_t state)
{
  uint16_t color = state ? UI_RELAY_ON : UI_RELAY_OFF;
  ILI9341_DrawText(name, FONT1, x, y, WHITE, UI_BG);
  ILI9341_DrawFilledCircle(x + 22U, y + 4U, 4U, color);
  ILI9341_DrawText(state ? "ON" : "OFF", FONT1, x + 32U, y, state ? UI_RELAY_ON : LIGHTGREY, UI_BG);
}

static void UI_DrawDashboard(void)
{
  char value[16];

  ILI9341_FillScreen(UI_BG);
  ILI9341_DrawRectangle(0, 0, 320, 30, UI_HEADER_BG);
  ILI9341_DrawText("MODE 1 - SENSOR MONITOR", FONT2, 55, 9, UI_TITLE_COLOR, UI_HEADER_BG);

  if (app.environment.sht30Valid != 0U) UI_FormatFixed1(app.environment.temperature10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
  UI_DrawSensorCard(6, 38, 151, 42, "TEMPERATURE", value, "C");

  if (app.environment.sht30Valid != 0U) UI_FormatFixed1(app.environment.humidity10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
  UI_DrawSensorCard(163, 38, 151, 42, "HUMIDITY", value, "%");

  if (app.environment.bme280Valid != 0U) UI_FormatFixed1(app.environment.pressure10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
  UI_DrawSensorCard(6, 86, 151, 42, "PRESSURE", value, "hPa");

  if (app.environment.sen0390Valid != 0U) (void)snprintf(value, sizeof(value), "%lu", (unsigned long)app.environment.lightLux); else (void)snprintf(value, sizeof(value), "ERR");
  UI_DrawSensorCard(163, 86, 151, 42, "LIGHT", value, "lx");

  (void)snprintf(value, sizeof(value), "%u", app.adc.mv[0]);
  UI_DrawSensorCard(6, 134, 151, 42, "ADC1 INPUT", value, "mV");

  (void)snprintf(value, sizeof(value), "%u", app.adc.mv[1]);
  UI_DrawSensorCard(163, 134, 151, 42, "ADC2 INPUT", value, "mV");

  ILI9341_DrawHLine(0, 184, 320, UI_CARD_BORDER);
  ILI9341_DrawText("NOISE", FONT1, 10, 192, LIGHTGREY, UI_BG);

  if (app.environment.sm7901bValid != 0U) UI_FormatFixed1(app.environment.noise10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
  ILI9341_DrawText(value, FONT1, 60, 192, WHITE, UI_BG);
  ILI9341_DrawText("dB", FONT1, 108, 192, LIGHTGREY, UI_BG);

  ILI9341_DrawText("DIGITAL INPUT", FONT1, 170, 192, LIGHTGREY, UI_BG);
  ILI9341_DrawText(app.digitalInput.state.bit.ch1 ? "DI1 ON" : "DI1 OFF", FONT1, 10, 215, app.digitalInput.state.bit.ch1 ? GREEN : LIGHTGREY, UI_BG);
  ILI9341_DrawText(app.digitalInput.state.bit.ch2 ? "DI2 ON" : "DI2 OFF", FONT1, 86, 215, app.digitalInput.state.bit.ch2 ? GREEN : LIGHTGREY, UI_BG);
  ILI9341_DrawText(app.digitalInput.state.bit.ch3 ? "DI3 ON" : "DI3 OFF", FONT1, 162, 215, app.digitalInput.state.bit.ch3 ? GREEN : LIGHTGREY, UI_BG);
  ILI9341_DrawText(app.digitalInput.state.bit.ch4 ? "DI4 ON" : "DI4 OFF", FONT1, 238, 215, app.digitalInput.state.bit.ch4 ? GREEN : LIGHTGREY, UI_BG);
}

static void UI_DrawModeMenu(void)
{
  ILI9341_FillScreen(UI_BG);
  ILI9341_DrawRectangle(0, 0, 320, 30, UI_HEADER_BG);
  ILI9341_DrawText("SMART DATALOGGER", FONT2, 80, 9, UI_TITLE_COLOR, UI_HEADER_BG);
  ILI9341_DrawText("SELECT MODE", FONT2, 112, 38, WHITE, UI_BG);

  ILI9341_DrawRectangle(25, 58, 270, 30, UI_CARD_BG);
  ILI9341_DrawText("MODE 1", FONT1, 40, 69, CYAN, UI_CARD_BG);
  ILI9341_DrawText("SENSOR MONITOR", FONT1, 125, 69, WHITE, UI_CARD_BG);

  ILI9341_DrawRectangle(25, 94, 270, 30, UI_CARD_BG);
  ILI9341_DrawText("MODE 2", FONT1, 40, 105, CYAN, UI_CARD_BG);
  ILI9341_DrawText("THERMAL CAMERA", FONT1, 125, 105, WHITE, UI_CARD_BG);

  ILI9341_DrawRectangle(25, 130, 270, 30, UI_CARD_BG);
  ILI9341_DrawText("MODE 3", FONT1, 40, 141, CYAN, UI_CARD_BG);
  ILI9341_DrawText("OUTPUT CONTROL", FONT1, 125, 141, WHITE, UI_CARD_BG);

  ILI9341_DrawRectangle(25, 166, 270, 30, UI_CARD_BG);
  ILI9341_DrawText("MODE 4", FONT1, 40, 177, CYAN, UI_CARD_BG);
  ILI9341_DrawText("RELAY TIMER", FONT1, 125, 177, WHITE, UI_CARD_BG);

  UI_DrawWebIp();
}

static void UI_DrawActiveControl(void)
{
  IO4State_t relay = {0};
  char dacCode[8];
  char dacVoltage[12];
  uint32_t dacMv = (app.dac.sample * ADC_VREF_MV) / ADC_MAX_VALUE;

  relay.bit.ch1 = (HAL_GPIO_ReadPin(GPIOC, Y1_OUTPUT_Pin) == GPIO_PIN_SET);
  relay.bit.ch2 = (HAL_GPIO_ReadPin(GPIOC, Y2_OUTPUT_Pin) == GPIO_PIN_SET);
  relay.bit.ch3 = (HAL_GPIO_ReadPin(GPIOB, Y3_OUTPUT_Pin) == GPIO_PIN_SET);
  relay.bit.ch4 = (HAL_GPIO_ReadPin(GPIOB, Y4_OUTPUT_Pin) == GPIO_PIN_SET);

  (void)snprintf(dacCode, sizeof(dacCode), "%lu", (unsigned long)app.dac.sample);
  (void)snprintf(dacVoltage, sizeof(dacVoltage), "%lu.%03lu", (unsigned long)(dacMv / 1000U), (unsigned long)(dacMv % 1000U));

  ILI9341_FillScreen(UI_BG);
  ILI9341_DrawRectangle(0, 0, 320, 30, UI_HEADER_BG);
  ILI9341_DrawText("MODE 3 - OUTPUT CONTROL", FONT2, 55, 9, UI_TITLE_COLOR, UI_HEADER_BG);
  ILI9341_DrawText("RELAY OUTPUT", FONT1, 10, 42, CYAN, UI_BG);
  UI_DrawRelay(10, 61, "R1", relay.bit.ch1);
  UI_DrawRelay(86, 61, "R2", relay.bit.ch2);
  UI_DrawRelay(162, 61, "R3", relay.bit.ch3);
  UI_DrawRelay(238, 61, "R4", relay.bit.ch4);
  ILI9341_DrawHLine(0, 82, 320, UI_CARD_BORDER);
  ILI9341_DrawText("ANALOG OUTPUT", FONT1, 10, 92, CYAN, UI_BG);
  ILI9341_DrawText("DAC1 CODE:", FONT1, 15, 111, LIGHTGREY, UI_BG);
  ILI9341_DrawText(dacCode, FONT2, 120, 107, WHITE, UI_BG);
  ILI9341_DrawText("DAC1 VOLT:", FONT1, 15, 130, LIGHTGREY, UI_BG);
  ILI9341_DrawText(dacVoltage, FONT2, 120, 126, WHITE, UI_BG);
  ILI9341_DrawText("V", FONT1, 190, 130, LIGHTGREY, UI_BG);
  ILI9341_DrawText("DAC1 STATE:", FONT1, 15, 150, LIGHTGREY, UI_BG);
  ILI9341_DrawText(app.dac.running ? "RUNNING" : "STOPPED", FONT1, 120, 150, app.dac.running ? GREEN : LIGHTGREY, UI_BG);
  ILI9341_DrawHLine(0, 165, 320, UI_CARD_BORDER);
  ILI9341_DrawText("COMMUNICATION OUTPUT", FONT1, 10, 174, CYAN, UI_BG);
  ILI9341_DrawText("CAN TX", FONT1, 15, 194, LIGHTGREY, UI_BG);
  ILI9341_DrawText("READY", FONT1, 85, 194, GREEN, UI_BG);
  ILI9341_DrawText("RS485 TX", FONT1, 165, 194, LIGHTGREY, UI_BG);
  ILI9341_DrawText("READY", FONT1, 245, 194, GREEN, UI_BG);
  ILI9341_DrawHLine(0, 209, 320, UI_CARD_BORDER);
  ILI9341_DrawText("LAST CMD:", FONT1, 10, 220, LIGHTGREY, UI_BG);
  ILI9341_DrawText("WAITING...", FONT1, 85, 220, CYAN, UI_BG);
}

static void UI_UpdateRelayTimer(uint32_t now)
{
  uint8_t relayIndex;
  char value[24];

  for (relayIndex = 0U; relayIndex < 4U; relayIndex++)
  {
    RelayTimerSlot_t *slot = &app.relayTimer.relay[relayIndex];
    uint16_t y = (uint16_t)(58U + (relayIndex * 30U));
    GPIO_PinState relayState;

    if (relayIndex == 0U) relayState = HAL_GPIO_ReadPin(GPIOC, Y1_OUTPUT_Pin);
    else if (relayIndex == 1U) relayState = HAL_GPIO_ReadPin(GPIOC, Y2_OUTPUT_Pin);
    else if (relayIndex == 2U) relayState = HAL_GPIO_ReadPin(GPIOB, Y3_OUTPUT_Pin);
    else relayState = HAL_GPIO_ReadPin(GPIOB, Y4_OUTPUT_Pin);

    ILI9341_DrawRectangle(10, y, 300, 22, UI_BG);
    (void)snprintf(value, sizeof(value), "R%u  %s", (unsigned int)(relayIndex + 1U), (relayState == GPIO_PIN_SET) ? "ON " : "OFF");
    ILI9341_DrawText(value, FONT1, 15, y + 5U, WHITE, UI_BG);

    if (slot->active != 0U)
    {
      uint32_t remaining = (slot->delayMs - (now - slot->startTick) + 999U) / 1000U;
      (void)snprintf(value, sizeof(value), "%lu sec -> %s", (unsigned long)remaining, slot->targetState ? "ON" : "OFF");
    }
    else
    {
      (void)snprintf(value, sizeof(value), "----");
    }

    ILI9341_DrawText(value, FONT1, 110, y + 5U, slot->active ? CYAN : LIGHTGREY, UI_BG);
  }
}

static void UI_DrawRelayTimer(void)
{
  ILI9341_FillScreen(UI_BG);
  ILI9341_DrawRectangle(0, 0, 320, 30, UI_HEADER_BG);
  ILI9341_DrawText("MODE 4 - RELAY TIMER", FONT2, 60, 9, UI_TITLE_COLOR, UI_HEADER_BG);
  ILI9341_DrawText("RELAY  STATE / PENDING ACTION", FONT1, 15, 42, UI_LABEL_COLOR, UI_BG);
  ILI9341_DrawText("RESET VIA ESP32", FONT1, 90, 210, LIGHTGREY, UI_BG);
  UI_UpdateRelayTimer(HAL_GetTick());
}

static void UI_DrawCurrentMode(void)
{
  switch (app.ui.currentMode)
  {
    case MODE_MENU: UI_DrawModeMenu(); break;
    case MODE_SENSOR: UI_DrawDashboard(); break;
    case MODE_THERMAL: UI_DrawThermalCamera(); break;
    case MODE_OUTPUT: UI_DrawActiveControl(); break;
    case MODE_TIMER: UI_DrawRelayTimer(); break;
    default:
      app.ui.currentMode = MODE_MENU;
      UI_DrawModeMenu();
      break;
  }
}

static void UI_DrawThermalCamera(void)
{
  ILI9341_FillScreen(UI_BG);
  ILI9341_DrawRectangle(0, 0, 320, 30, UI_HEADER_BG);
  ILI9341_DrawText("MODE 2 - THERMAL CAMERA", FONT2, 50, 9, UI_TITLE_COLOR, UI_HEADER_BG);
  ILI9341_DrawRectangle(THERMAL_IMAGE_X - 2U, THERMAL_IMAGE_Y - 2U, 260, 148, UI_CARD_BG);
  ILI9341_DrawHollowRectangleCoord(THERMAL_IMAGE_X - 2U, THERMAL_IMAGE_Y - 2U, THERMAL_IMAGE_X + 257U, THERMAL_IMAGE_Y + 145U, UI_CARD_BORDER);
  ILI9341_DrawText("MLX90640", FONT1, 5, 190, UI_LABEL_COLOR, UI_BG);
  ILI9341_DrawText(thermal.initialized ? "LIVE" : "CONNECTING", FONT1, 80, 190, thermal.initialized ? GREEN : LIGHTGREY, UI_BG);
  ILI9341_DrawText("MIN", FONT1, 8, 214, LIGHTGREY, UI_BG);
  ILI9341_DrawText("--.-", FONT1, 38, 214, WHITE, UI_BG);
  ILI9341_DrawText("CTR", FONT1, 108, 214, LIGHTGREY, UI_BG);
  ILI9341_DrawText("--.-", FONT1, 138, 214, WHITE, UI_BG);
  ILI9341_DrawText("MAX", FONT1, 208, 214, LIGHTGREY, UI_BG);
  ILI9341_DrawText("--.-", FONT1, 238, 214, WHITE, UI_BG);
}

static void UI_Init(void)
{
  ILI9341_SetRotation(SCREEN_HORIZONTAL_2);
  HAL_Delay(20);
  app.ui.currentMode = MODE_MENU;
  app.ui.previousMode = MODE_MENU;
  app.ui.lastUiTick = 0U;
  UI_DrawModeMenu();
}

static uint16_t Thermal_GetColor(float temperature, float minTemperature, float maxTemperature)
{
  float normalized;
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  if (maxTemperature <= minTemperature) return WHITE;

  normalized = (temperature - minTemperature) / (maxTemperature - minTemperature);
  if (normalized < 0.0f) normalized = 0.0f;
  if (normalized > 1.0f) normalized = 1.0f;

  if (normalized < 0.25f)
  {
    red = 0U;
    green = (uint8_t)(normalized * 4.0f * 255.0f);
    blue = 255U;
  }
  else if (normalized < 0.50f)
  {
    red = 0U;
    green = 255U;
    blue = (uint8_t)((0.50f - normalized) * 4.0f * 255.0f);
  }
  else if (normalized < 0.75f)
  {
    red = (uint8_t)((normalized - 0.50f) * 4.0f * 255.0f);
    green = 255U;
    blue = 0U;
  }
  else
  {
    red = 255U;
    green = (uint8_t)((1.0f - normalized) * 4.0f * 255.0f);
    blue = 0U;
  }

  return (uint16_t)(((uint16_t)(red & 0xF8U) << 8) | ((uint16_t)(green & 0xFCU) << 3) | ((uint16_t)blue >> 3));
}

static void Thermal_FormatTemperature(float temperature, char *buffer, uint16_t bufferSize)
{
  int32_t temperature10 = (int32_t)(temperature * 10.0f);
  uint32_t absoluteTemperature = (temperature10 < 0) ? (uint32_t)(-temperature10) : (uint32_t)temperature10;
  (void)snprintf(buffer, bufferSize, "%s%lu.%lu", (temperature10 < 0) ? "-" : "", (unsigned long)(absoluteTemperature / 10U), (unsigned long)(absoluteTemperature % 10U));
}

static void Thermal_UpdateStatistics(void)
{
  float minimum = thermal.temperature[0];
  float maximum = thermal.temperature[0];
  uint16_t i;

  for (i = 1U; i < 768U; i++)
  {
    if (thermal.temperature[i] < minimum) minimum = thermal.temperature[i];
    if (thermal.temperature[i] > maximum) maximum = thermal.temperature[i];
  }

  thermal.minTemp = minimum;
  thermal.maxTemp = maximum;
  thermal.centerTemp = (thermal.temperature[(11U * 32U) + 15U] + thermal.temperature[(11U * 32U) + 16U] + thermal.temperature[(12U * 32U) + 15U] + thermal.temperature[(12U * 32U) + 16U]) * 0.25f;
}

static void Thermal_DrawFrame(void)
{
  char value[16];
  float scaleMin = thermal.minTemp - 1.0f;
  float scaleMax = thermal.maxTemp + 1.0f;
  uint16_t row;
  uint16_t column;

  for (row = 0U; row < 24U; row++)
  {
    for (column = 0U; column < 32U; column++)
    {
      uint16_t pixel = (row * 32U) + column;
      uint16_t x = THERMAL_IMAGE_X + (column * THERMAL_PIXEL_WIDTH);
      uint16_t y = THERMAL_IMAGE_Y + (row * THERMAL_PIXEL_HEIGHT);
      uint16_t color = Thermal_GetColor(thermal.temperature[pixel], scaleMin, scaleMax);
      ILI9341_DrawRectangle(x, y, THERMAL_PIXEL_WIDTH, THERMAL_PIXEL_HEIGHT, color);
    }
  }

  ILI9341_DrawRectangle(35, 207, 58, 25, UI_BG);
  Thermal_FormatTemperature(thermal.minTemp, value, sizeof(value));
  ILI9341_DrawText(value, FONT1, 38, 214, WHITE, UI_BG);

  ILI9341_DrawRectangle(135, 207, 58, 25, UI_BG);
  Thermal_FormatTemperature(thermal.centerTemp, value, sizeof(value));
  ILI9341_DrawText(value, FONT1, 138, 214, WHITE, UI_BG);

  ILI9341_DrawRectangle(235, 207, 58, 25, UI_BG);
  Thermal_FormatTemperature(thermal.maxTemp, value, sizeof(value));
  ILI9341_DrawText(value, FONT1, 238, 214, WHITE, UI_BG);

  ILI9341_DrawRectangle(76, 186, 100, 14, UI_BG);
  ILI9341_DrawText("LIVE", FONT1, 80, 190, GREEN, UI_BG);
}

static int Thermal_Init(void)
{
  int result;

  thermal.initialized = 0U;
  thermal.frameReady = 0U;
  thermal.frameSequence = 0U;
  thermal.error = MLX_ERR_NONE;
  thermal.frameErrorCount = 0U;
  thermal.subpageMask = 0U;

  if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(MLX90640_ADDRESS << 1), 3U, 100U) != HAL_OK)
  {
    thermal.lastError = -10;
    thermal.error = MLX_ERR_I2C_NOT_READY;
    return -10;
  }

  MLX90640_I2CInit();

  result = MLX90640_DumpEE(MLX90640_ADDRESS, thermal.frameData);
  if (result != 0)
  {
    thermal.lastError = (int16_t)result;
    thermal.error = MLX_ERR_DUMP_EE;
    return result;
  }

  result = MLX90640_ExtractParameters(thermal.frameData, &thermal.params);
  if (result != 0)
  {
    thermal.lastError = (int16_t)result;
    thermal.error = MLX_ERR_EXTRACT_PARAMS;
    return result;
  }

  result = MLX90640_SetRefreshRate(MLX90640_ADDRESS, 3U);
  if (result != 0)
  {
    thermal.lastError = (int16_t)result;
    thermal.error = MLX_ERR_SET_REFRESH;
    return result;
  }

  result = MLX90640_SetChessMode(MLX90640_ADDRESS);
  if (result != 0)
  {
    thermal.lastError = (int16_t)result;
    thermal.error = MLX_ERR_SET_MODE;
    return result;
  }

  thermal.lastError = 0;
  thermal.error = MLX_ERR_NONE;
  thermal.frameErrorCount = 0U;
  thermal.initialized = 1U;
  return 0;
}

static void Thermal_Update(uint32_t now)
{
  int result;
  uint8_t subpage;
  float reflectedTemperature;

  if (!thermal.initialized)
  {
    if ((now - thermal.lastInitTick) < THERMAL_RETRY_PERIOD_MS) return;
    thermal.lastInitTick = now;

    if (Thermal_Init() != 0)
    {
      ILI9341_DrawRectangle(76, 186, 150, 14, UI_BG);
      ILI9341_DrawText("MLX ERROR", FONT1, 80, 190, RED, UI_BG);
      return;
    }

    ILI9341_DrawRectangle(76, 186, 150, 14, UI_BG);
    ILI9341_DrawText("CONNECTED", FONT1, 80, 190, GREEN, UI_BG);
  }

  if ((now - thermal.lastFrameTick) < THERMAL_FRAME_PERIOD_MS) return;
  thermal.lastFrameTick = now;

  result = MLX90640_GetFrameData(MLX90640_ADDRESS, thermal.frameData);

  if (result < 0)
  {
    thermal.lastError = (int16_t)result;
    thermal.error = MLX_ERR_FRAME;
    if (thermal.frameErrorCount < 255U) thermal.frameErrorCount++;
    return;
  }

  thermal.lastError = 0;
  thermal.error = MLX_ERR_NONE;
  thermal.frameErrorCount = 0U;
  subpage = (uint8_t)result;
  thermal.ambientTemp = MLX90640_GetTa(thermal.frameData, &thermal.params);
  reflectedTemperature = thermal.ambientTemp - MLX90640_TA_SHIFT;
  MLX90640_CalculateTo(thermal.frameData, &thermal.params, MLX90640_EMISSIVITY, reflectedTemperature, thermal.temperature);
  MLX90640_BadPixelsCorrection(thermal.params.brokenPixels, thermal.temperature, 1, &thermal.params);
  MLX90640_BadPixelsCorrection(thermal.params.outlierPixels, thermal.temperature, 1, &thermal.params);

  if (subpage < 2U) thermal.subpageMask |= (uint8_t)(1U << subpage);

  if (thermal.subpageMask == 0x03U)
  {
    thermal.subpageMask = 0U;
    Thermal_UpdateStatistics();
    thermal.frameSequence++;
    thermal.frameReady = 1U;
    Thermal_DrawFrame();
  }
}

static void DigitalInput_Update(uint32_t now)
{
  if ((now - app.digitalInput.lastReadTick) < DIGITAL_READ_PERIOD_MS) return;

  app.digitalInput.lastReadTick = now;
  app.digitalInput.state.bit.ch1 = (HAL_GPIO_ReadPin(GPIOC, X1_INPUT_Pin) == GPIO_PIN_SET);
  app.digitalInput.state.bit.ch2 = (HAL_GPIO_ReadPin(GPIOA, X2_INPUT_Pin) == GPIO_PIN_SET);
  app.digitalInput.state.bit.ch3 = (HAL_GPIO_ReadPin(GPIOA, X3_INPUT_Pin) == GPIO_PIN_SET);
  app.digitalInput.state.bit.ch4 = (HAL_GPIO_ReadPin(GPIOA, X4_INPUT_Pin) == GPIO_PIN_SET);
}

static void ADC_Update(uint32_t now)
{
  if ((now - app.adc.lastReadTick) < ADC_READ_PERIOD_MS) return;
  app.adc.lastReadTick = now;

  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 2U) == HAL_OK) app.adc.mv[0] = (uint16_t)(((uint32_t)HAL_ADC_GetValue(&hadc1) * ADC_VREF_MV) / ADC_MAX_VALUE);
  HAL_ADC_Stop(&hadc1);

  HAL_ADC_Start(&hadc2);
  if (HAL_ADC_PollForConversion(&hadc2, 2U) == HAL_OK) app.adc.mv[1] = (uint16_t)(((uint32_t)HAL_ADC_GetValue(&hadc2) * ADC_VREF_MV) / ADC_MAX_VALUE);
  HAL_ADC_Stop(&hadc2);
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_CAN_Init();
  MX_DAC_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USART3_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  Sensors_Init();
  DataLogger_Init(&logBuffer);
  Scheduler_Init(&scheduler, HAL_GetTick(), DATALOGGER_LOG_INTERVAL_MS, DATALOGGER_UPLOAD_INTERVAL_MS);
  Scheduler_Enable(&scheduler, 0U);

  ILI9341_Init();
  HAL_Delay(100U);
  UI_Init();

  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_ADCEx_Calibration_Start(&hadc2);
  (void)HAL_UART_Receive_IT(&huart2, &esp32Link.rxByte, 1U);
  /* USER CODE END 2 */

  while (1)
  {
    uint32_t now = HAL_GetTick();
    uint8_t modeChanged;

    ESP32_ProcessCommands(now);
    Sensors_Update(now);
    DigitalInput_Update(now);
    ADC_Update(now);
    RelayTimer_Process(now);
    DataLogger_Process(now);

    modeChanged = (app.ui.currentMode != app.ui.previousMode);

    if (modeChanged != 0U)
    {
      UI_DrawCurrentMode();
      app.ui.previousMode = app.ui.currentMode;
    }

    switch (app.ui.currentMode)
    {
      case MODE_MENU:
        break;

      case MODE_SENSOR:
      {
        char value[16];

        if ((now - app.ui.lastUiTick) >= UI_REFRESH_PERIOD_MS)
        {
          app.ui.lastUiTick = now;

          if (app.environment.sht30Valid != 0U) UI_FormatFixed1(app.environment.temperature10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
          UI_DrawSensorCard(6, 38, 151, 42, "TEMPERATURE", value, "C");

          if (app.environment.sht30Valid != 0U) UI_FormatFixed1(app.environment.humidity10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
          UI_DrawSensorCard(163, 38, 151, 42, "HUMIDITY", value, "%");

          if (app.environment.bme280Valid != 0U) UI_FormatFixed1(app.environment.pressure10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
          UI_DrawSensorCard(6, 86, 151, 42, "PRESSURE", value, "hPa");

          if (app.environment.sen0390Valid != 0U) (void)snprintf(value, sizeof(value), "%lu", (unsigned long)app.environment.lightLux); else (void)snprintf(value, sizeof(value), "ERR");
          UI_DrawSensorCard(163, 86, 151, 42, "LIGHT", value, "lx");

          (void)snprintf(value, sizeof(value), "%u", app.adc.mv[0]);
          UI_DrawSensorCard(6, 134, 151, 42, "ADC1 INPUT", value, "mV");

          (void)snprintf(value, sizeof(value), "%u", app.adc.mv[1]);
          UI_DrawSensorCard(163, 134, 151, 42, "ADC2 INPUT", value, "mV");

          ILI9341_DrawRectangle(55, 188, 50, 16, UI_BG);
          if (app.environment.sm7901bValid != 0U) UI_FormatFixed1(app.environment.noise10, value, sizeof(value)); else (void)snprintf(value, sizeof(value), "ERR");
          ILI9341_DrawText(value, FONT1, 60, 192, WHITE, UI_BG);

          ILI9341_DrawText(app.digitalInput.state.bit.ch1 ? "DI1 ON" : "DI1 OFF", FONT1, 10, 215, app.digitalInput.state.bit.ch1 ? GREEN : LIGHTGREY, UI_BG);
          ILI9341_DrawText(app.digitalInput.state.bit.ch2 ? "DI2 ON" : "DI2 OFF", FONT1, 86, 215, app.digitalInput.state.bit.ch2 ? GREEN : LIGHTGREY, UI_BG);
          ILI9341_DrawText(app.digitalInput.state.bit.ch3 ? "DI3 ON" : "DI3 OFF", FONT1, 162, 215, app.digitalInput.state.bit.ch3 ? GREEN : LIGHTGREY, UI_BG);
          ILI9341_DrawText(app.digitalInput.state.bit.ch4 ? "DI4 ON" : "DI4 OFF", FONT1, 238, 215, app.digitalInput.state.bit.ch4 ? GREEN : LIGHTGREY, UI_BG);
        }
        break;
      }

      case MODE_THERMAL:
        Thermal_Update(now);
        break;

      case MODE_OUTPUT:
      {
        static uint8_t oldRelayState = 0xFFU;
        IO4State_t relay = {0};

        relay.bit.ch1 = (HAL_GPIO_ReadPin(GPIOC, Y1_OUTPUT_Pin) == GPIO_PIN_SET);
        relay.bit.ch2 = (HAL_GPIO_ReadPin(GPIOC, Y2_OUTPUT_Pin) == GPIO_PIN_SET);
        relay.bit.ch3 = (HAL_GPIO_ReadPin(GPIOB, Y3_OUTPUT_Pin) == GPIO_PIN_SET);
        relay.bit.ch4 = (HAL_GPIO_ReadPin(GPIOB, Y4_OUTPUT_Pin) == GPIO_PIN_SET);

        {
          uint8_t changed = relay.all ^ oldRelayState;
          if (((changed & (1U << 0)) != 0U) || (modeChanged != 0U)) UI_DrawRelay(10, 61, "R1", relay.bit.ch1);
          if (((changed & (1U << 1)) != 0U) || (modeChanged != 0U)) UI_DrawRelay(86, 61, "R2", relay.bit.ch2);
          if (((changed & (1U << 2)) != 0U) || (modeChanged != 0U)) UI_DrawRelay(162, 61, "R3", relay.bit.ch3);
          if (((changed & (1U << 3)) != 0U) || (modeChanged != 0U)) UI_DrawRelay(238, 61, "R4", relay.bit.ch4);
        }

        oldRelayState = relay.all;

        if ((now - app.ui.lastUiTick) >= UI_REFRESH_PERIOD_MS)
        {
          char dacCode[8];
          char dacVoltage[12];
          uint32_t dacMv = (app.dac.sample * ADC_VREF_MV) / ADC_MAX_VALUE;

          app.ui.lastUiTick = now;
          (void)snprintf(dacCode, sizeof(dacCode), "%lu", (unsigned long)app.dac.sample);
          (void)snprintf(dacVoltage, sizeof(dacVoltage), "%lu.%03lu", (unsigned long)(dacMv / 1000U), (unsigned long)(dacMv % 1000U));

          ILI9341_DrawRectangle(115, 104, 85, 20, UI_BG);
          ILI9341_DrawText(dacCode, FONT2, 120, 107, WHITE, UI_BG);
          ILI9341_DrawRectangle(115, 123, 105, 21, UI_BG);
          ILI9341_DrawText(dacVoltage, FONT2, 120, 126, WHITE, UI_BG);
          ILI9341_DrawText("V", FONT1, 190, 130, LIGHTGREY, UI_BG);
          ILI9341_DrawRectangle(115, 146, 100, 16, UI_BG);
          ILI9341_DrawText(app.dac.running ? "RUNNING" : "STOPPED", FONT1, 120, 150, app.dac.running ? GREEN : LIGHTGREY, UI_BG);
        }
        break;
      }

      case MODE_TIMER:
        if ((now - app.ui.lastUiTick) >= UI_REFRESH_PERIOD_MS)
        {
          app.ui.lastUiTick = now;
          UI_UpdateRelayTimer(now);
        }
        break;

      default:
        app.ui.currentMode = MODE_MENU;
        break;
    }

    ESP32_SendSensorData(now);
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

static void MX_ADC2_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc2.Instance = ADC2;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK) Error_Handler();

  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) Error_Handler();
}

static void MX_CAN_Init(void)
{
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 16;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_1TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK) Error_Handler();
}

static void MX_DAC_Init(void)
{
  DAC_ChannelConfTypeDef sConfig = {0};

  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK) Error_Handler();

  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_I2C2_Init(void)
{
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK) Error_Handler();
}

static void MX_SPI2_Init(void)
{
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK) Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_USART3_Init(void)
{
  husart3.Instance = USART3;
  husart3.Init.BaudRate = 115200;
  husart3.Init.WordLength = USART_WORDLENGTH_8B;
  husart3.Init.StopBits = USART_STOPBITS_1;
  husart3.Init.Parity = USART_PARITY_NONE;
  husart3.Init.Mode = USART_MODE_TX_RX;
  husart3.Init.CLKPolarity = USART_POLARITY_LOW;
  husart3.Init.CLKPhase = USART_PHASE_1EDGE;
  husart3.Init.CLKLastBit = USART_LASTBIT_DISABLE;
  if (HAL_USART_Init(&husart3) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
  HAL_NVIC_SetPriority(DMA2_Channel3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel3_IRQn);
  HAL_NVIC_SetPriority(DMA2_Channel4_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, Y1_OUTPUT_Pin | Y2_OUTPUT_Pin | DC_Pin | RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, Y3_OUTPUT_Pin | Y4_OUTPUT_Pin | RS485_NOISE_DIR_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = Y1_OUTPUT_Pin | Y2_OUTPUT_Pin | DC_Pin | RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = Y3_OUTPUT_Pin | Y4_OUTPUT_Pin | RS485_NOISE_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BUSY_Pin | X1_INPUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = X2_INPUT_Pin | X3_INPUT_Pin | X4_INPUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
