// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"
#include "Core/HW/SI/SI_Device.h"

namespace SerialInterface
{
class CSIDevice_DebugUART final : public ISIDevice
{
  int m_str_pos = 0;
  char m_str_buffer[512] = {};

public:
  CSIDevice_DebugUART(Core::System& system, SIDevices device, int device_number);

  int RunBuffer(u8* buffer, int request_length) override;
  DataResponse GetData(u32& hi, u32& low) override;
  void SendCommand(u32 command, u8 poll) override;
};
}  // namespace SerialInterface
