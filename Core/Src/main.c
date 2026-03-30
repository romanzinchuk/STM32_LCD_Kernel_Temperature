/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MCU Core Temperature Monitoring System with ILI9341 Display
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Roman Zinchuk.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
#define GRAPH_START_X  20
#define GRAPH_WIDTH    220  // Total screen width (240) - axis labels offset (20)
#define GRAPH_HEIGHT   100
#define GRAPH_Y_BOTTOM 200  // Lower boundary of the graph on screen

uint16_t graph_points[GRAPH_WIDTH] = {0}; // Buffer for historical Y-coordinates
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Sends a command to the ILI9341 controller (DC pin Low)
  */
void TFT_WriteCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/**
  * @brief Sends data to the ILI9341 controller (DC pin High)
  */
void TFT_WriteData(uint8_t data) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &data, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/**
  * @brief Hardware reset for the TFT display
  */
void TFT_Reset(void) {
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
}

/**
  * @brief Sets the address window for drawing
  */
void TFT_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    TFT_WriteCommand(0x2A); // Column Address Set
    TFT_WriteData(x0 >> 8);
    TFT_WriteData(x0 & 0xFF);
    TFT_WriteData(x1 >> 8);
    TFT_WriteData(x1 & 0xFF);

    TFT_WriteCommand(0x2B); // Page Address Set
    TFT_WriteData(y0 >> 8);
    TFT_WriteData(y0 & 0xFF);
    TFT_WriteData(y1 >> 8);
    TFT_WriteData(y1 & 0xFF);
}

/**
  * @brief Draws a single pixel with RGB565 color
  */
void TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    TFT_SetWindow(x, y, x, y);
    TFT_WriteCommand(0x2C);      // Memory Write
    TFT_WriteData(color >> 8);
    TFT_WriteData(color & 0xFF);
}

/**
  * @brief Fills the entire screen with a specific color
  */
void TFT_FillScreen(uint16_t color) {
    TFT_SetWindow(0, 0, 239, 319);
    TFT_WriteCommand(0x2C);
    for(uint32_t i = 0; i < 240 * 320; i++) {
        TFT_WriteData(color >> 8);
        TFT_WriteData(color & 0xFF);
    }
}

/**
  * @brief Initialization sequence for ILI9341 display
  */
void TFT_Init(void) {
    TFT_Reset();
    TFT_WriteCommand(0x01); // Software Reset
    HAL_Delay(100);
    TFT_WriteCommand(0x11); // Sleep Out
    HAL_Delay(100);
    TFT_WriteCommand(0x3A); // Pixel Format Set
    TFT_WriteData(0x55);    // 16-bit RGB565
    TFT_WriteCommand(0x36); // Memory Access Control
    TFT_WriteData(0x48);    // Orientation settings
    TFT_WriteCommand(0x29); // Display ON
    HAL_Delay(100);
}

/**
  * @brief Reads internal sensor and applies moving average filter
  * @return Filtered ADC raw value
  */
uint32_t Get_Filtered_Temperature(void) {
    uint32_t sum = 0;
    const uint8_t samples = 128;

    for(uint8_t i = 0; i < samples; i++) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum += HAL_ADC_GetValue(&hadc1);
    }
    return sum / samples;
}

/* 5x7 Font definition for digits '0'-'9' */
const uint8_t font_digits[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
};

void TFT_DrawDigit(uint16_t x, uint16_t y, uint8_t digit, uint16_t color) {
    if(digit > 9) return;
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = font_digits[digit][i];
        for (uint8_t j = 0; j < 8; j++) {
            if (line & (1 << j)) {
                TFT_DrawPixel(x + i, y + j, color);
            }
        }
    }
}

void TFT_DrawNumber(uint16_t x, uint16_t y, uint8_t num, uint16_t color) {
    TFT_DrawDigit(x, y, num / 10, color);       // Tens
    TFT_DrawDigit(x + 6, y, num % 10, color);   // Units
}

/**
  * @brief Draws static coordinate grid and scale labels
  */
void TFT_DrawGrid(void) {
    // Vertical Y-axis
    for(uint16_t y = 100; y <= 200; y++) TFT_DrawPixel(18, y, 0xFFFF);
    // Horizontal X-axis
    for(uint16_t x = 18; x < 240; x++) TFT_DrawPixel(x, 200, 0xFFFF);

    // Grid labels (Temperature estimates)
    TFT_DrawNumber(2, 96, 50, 0xFFFF);  // Top: 50C
    for(uint16_t x = 18; x < 240; x+=4) TFT_DrawPixel(x, 100, 0x4208);

    TFT_DrawNumber(2, 146, 40, 0xFFFF); // Mid: 40C
    for(uint16_t x = 18; x < 240; x+=4) TFT_DrawPixel(x, 150, 0x4208);

    TFT_DrawNumber(2, 196, 30, 0xFFFF); // Bottom: 30C
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  /* Initialize peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  TFT_Init();
  TFT_FillScreen(0x0000); // Clear background
  TFT_DrawGrid();         // Static background UI

  // Initialize graph data points at bottom position
  for(int i = 0; i < GRAPH_WIDTH; i++) {
      graph_points[i] = GRAPH_Y_BOTTOM;
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      // 1. Fetch filtered sensor data
      uint32_t raw_temp = Get_Filtered_Temperature();

      // 2. Data scaling and offset adjustment (925 is the tuned baseline)
      int32_t display_val = (raw_temp - 925) * 4;
      if (display_val < 0) display_val = 0;
      if (display_val > GRAPH_HEIGHT) display_val = GRAPH_HEIGHT;
      uint16_t scaled_y = GRAPH_Y_BOTTOM - (uint16_t)display_val;

      // 3. Shift graph array left and redraw
      for(uint16_t i = 0; i < GRAPH_WIDTH - 1; i++) {
          // Erase old pixel while preserving grid lines
          if (graph_points[i] == 100 || graph_points[i] == 150) {
              TFT_DrawPixel(GRAPH_START_X + i, graph_points[i], 0x4208); // Restore grid
          } else if (graph_points[i] == 200) {
              TFT_DrawPixel(GRAPH_START_X + i, graph_points[i], 0xFFFF); // Restore axis
          } else {
              TFT_DrawPixel(GRAPH_START_X + i, graph_points[i], 0x0000); // Background
          }

          // Shift buffer
          graph_points[i] = graph_points[i + 1];

          // Draw shifted green point
          TFT_DrawPixel(GRAPH_START_X + i, graph_points[i], 0x07E0);
      }

      // 4. Update the latest data point at the end of buffer
      graph_points[GRAPH_WIDTH - 1] = scaled_y;
      TFT_DrawPixel(GRAPH_START_X + GRAPH_WIDTH - 1, scaled_y, 0x07E0);

      HAL_Delay(50); // Frame rate control
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
