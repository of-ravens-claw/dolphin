// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/SI/SI_DeviceDebugUART.h"

#include "Common/Logging/Log.h"
#include "Common/Swap.h"

namespace SerialInterface
{
CSIDevice_DebugUART::CSIDevice_DebugUART(Core::System& system, SIDevices device, int device_number)
    : ISIDevice{system, device, device_number}
{
}

int CSIDevice_DebugUART::RunBuffer(u8* buffer, int request_length)
{
  ISIDevice::RunBuffer(buffer, request_length);

  {
    // Read the command
    const auto command = static_cast<EBufferCommands>(buffer[0]);

    // Handle it
    switch (command)
    {
    case EBufferCommands::CMD_STATUS:
    case EBufferCommands::CMD_RESET:
    {
      return CreateStatusResponse(0x0842AF00, buffer);
    }

    case EBufferCommands::CMD_DIRECT_N64: // CMD_UART_TXCNT
    {
      buffer[0] = 0; // TX buffer empty
      return 1;
    }

    case EBufferCommands::CMD_READ_ACCESSORY_N64: // CMD_UART_TXD
    {
      const u8 ch = buffer[1];
      //buffer[0] = ch;
      //buffer[1] = ~ch;
      buffer[0] = 0x04; // Sent succesfully!
      buffer[1] = 0x00;
      m_str_buffer[m_str_pos++] = static_cast<char>(ch);
      DEBUG_LOG_FMT(SERIALINTERFACE, "DEBUG_UART {} TX byte: '0x{:X}'", m_device_number, ch);

      if (ch == '\0' || m_str_pos >= sizeof(m_str_buffer) - 1)
      {
        // Search for ONE newline and replace with null terminator
        for (int i = m_str_pos - 1; i >= 0; i--)
        {
          if (m_str_buffer[i] == '\n')
          {
            DEBUG_LOG_FMT(SERIALINTERFACE, "DEBUG_UART {} Found newline at pos {}", m_device_number, i);
            m_str_buffer[i] = '\0';
            break;
          }
        }

        m_str_buffer[m_str_pos] = '\0';
        NOTICE_LOG_FMT(SERIALINTERFACE, "DEBUG_UART {} TX string: '{}'", m_device_number, m_str_buffer);
        m_str_pos = 0;
        memset(m_str_buffer, 0, sizeof(m_str_buffer));
      }

      return 2;
    }

    default:
      break;
    }
  }

  return -1;
}

DataResponse CSIDevice_DebugUART::GetData(u32& hi, u32& low)
{
  return DataResponse::ErrorNoResponse;
}

void CSIDevice_DebugUART::SendCommand(u32 command, u8 poll)
{
}
}  // namespace SerialInterface
