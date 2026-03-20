// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/SI/SI_DeviceN64Mouse.h"

#include "Common/BitField.h"
#include "Common/CommonTypes.h"

#include "Common/Swap.h"
#include "Common/Logging/Log.h"

namespace SerialInterface
{
CSIDevice_N64Mouse::CSIDevice_N64Mouse(Core::System& system, SIDevices device, int device_number)
    : ISIDevice{system, device, device_number}
{
}

union MouseSIData
{
  u32 hex = 0;

  BitField<0, 8, s32> relative_x;
  BitField<8, 8, s32> relative_y;
  BitField<30, 1, u32> btn_left;
  BitField<31, 1, u32> btn_right;

#if 0
  struct
  {
    u32 btn_left   : 1;  // A
    u32 btn_right  : 1;  // B
    u32 reserved   : 14; // not used for anything, always 0
    s32 relative_x : 8;
    s32 relative_y : 8;
  };
#endif
};
static_assert(sizeof(MouseSIData) == sizeof(u32), "wrong size");

int CSIDevice_N64Mouse::RunBuffer(u8* buffer, int request_length)
{
  // For debug logging only
  ISIDevice::RunBuffer(buffer, request_length);

  // Read the command
  const auto command = static_cast<EBufferCommands>(buffer[0]);

  // Handle it
  switch (command)
  {
  case EBufferCommands::CMD_STATUS:
  case EBufferCommands::CMD_RESET:
  {
    return CreateStatusResponse(SI_N64_MOUSE, buffer);
  }

  case EBufferCommands::CMD_DIRECT_N64:
  {
    INFO_LOG_FMT(SERIALINTERFACE, "N64 - Direct (Request length: {})", request_length);
    MouseSIData test;
    test.btn_left = false;
    test.btn_right = false;
    test.relative_x = -64;
    test.relative_y = +64;
    u32 data = test.hex;
    for (int i = 0; i < 4; i++)
    {
      buffer[i] = (data >> (24 - (i * 8))) & 0xff;
    }
    return sizeof(data);
  }

  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE, "Unknown SI command     ({:#x})", static_cast<u8>(command));
  }
  break;
  }

  return 0;
}

DataResponse CSIDevice_N64Mouse::GetData(u32& hi, u32& low)
{
  return DataResponse::NoData;
}

void CSIDevice_N64Mouse::SendCommand(u32 command, u8 poll)
{

}
}  // namespace SerialInterface
