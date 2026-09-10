/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_mem.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Utility functions for memory transfers to/from PMA
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"


/*******************************************************************************
 * @fn           UserToPMABufferCopy
 *
 * @brief        Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param        pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes: no. of bytes to be copied.
 *
 * @param        None	.
 */
void UserToPMABufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint16_t *pdwVal = (uint16_t *)(wPMABufAddr * 2U + PMAAddr);

  while (wNBytes >= 2U)
  {
    *pdwVal = (uint16_t)pbUsrBuf[0] | ((uint16_t)pbUsrBuf[1] << 8U);
    pdwVal += 2;
    pbUsrBuf += 2;
    wNBytes -= 2U;
  }

  if (wNBytes != 0U)
  {
    /* BOS 为 33 字节，末字只能单独读取，不能越过 Flash 数组边界。 */
    *pdwVal = (uint16_t)pbUsrBuf[0];
  }
}

/*******************************************************************************
 * @fn          PMAToUserBufferCopy
 *
 * @brief       Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param       pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes:  no. of bytes to be copied.
 *
 * @param       None. 
 */
void PMAToUserBufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint32_t *pdwVal = (uint32_t *)(wPMABufAddr * 2U + PMAAddr);

  while (wNBytes >= 2U)
  {
    const uint16_t pma_word = (uint16_t)*pdwVal++;
    *pbUsrBuf++ = (uint8_t)(pma_word & 0x00FFU);
    *pbUsrBuf++ = (uint8_t)((pma_word >> 8U) & 0x00FFU);
    wNBytes -= 2U;
  }

  if (wNBytes != 0U)
  {
    *pbUsrBuf = (uint8_t)((uint16_t)*pdwVal & 0x00FFU);
  }
}






