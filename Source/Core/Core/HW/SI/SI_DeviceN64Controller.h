// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"
#include "Core/HW/SI/SI_Device.h"
#include "InputCommon/GCPadStatus.h" // temporary...

namespace SerialInterface
{

class CSIDevice_N64Controller : public ISIDevice
{
public:
  // Constructor
  CSIDevice_N64Controller(Core::System& system, SIDevices device, int device_number);

  // Run the SI Buffer
  int RunBuffer(u8* buffer, int request_length) override;

  DataResponse GetData(u32& hi, u32& low) override;

  // Send a command directly
  void SendCommand(u32 command, u8 poll) override;

  virtual GCPadStatus GetPadStatus();
};

}  // namespace SerialInterface
