// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/SI/SI_DeviceN64Controller.h"

#include <cstring>

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Swap.h"
#include "Core/Config/MainSettings.h"
#include "Core/CoreTiming.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/SystemTimers.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/System.h"
#include "InputCommon/GCPadStatus.h"

// This repository has been used for reference on the N64 Joybus side.
// Unfortunately, how a GameCube would receive the data is unknown.
// https://github.com/raphnet/gc_n64_usb
//
// See D:\nintendo_gigaleak\nintendo_gigaleak\october_leak\NintendoLeakOctober\wiidump\BOST\DIAG\build\libraries\artx\src\n64cnt.c
// for the GameCube implementation.

namespace SerialInterface
{
// --- standard Nintendo 64 controller ---
CSIDevice_N64Controller::CSIDevice_N64Controller(Core::System& system, SIDevices device,
                                               int device_number)
    : ISIDevice(system, device, device_number)
{
}

GCPadStatus CSIDevice_N64Controller::GetPadStatus()
{
  GCPadStatus pad_status = Pad::GetStatus(m_device_number);
  return pad_status;
}

#define N64_BUTTON_A 0x8000
#define N64_BUTTON_B 0x4000
#define N64_TRIGGER_Z 0x2000
#define N64_BUTTON_START 0x1000
#define N64_BUTTON_UP 0x0800
#define N64_BUTTON_DOWN 0x0400
#define N64_BUTTON_LEFT 0x0200
#define N64_BUTTON_RIGHT 0x0100
#define N64_RESET 0x0080    // We should handle this, for now, set to zero
#define N64_UNKNOWN 0x0040  // Always set to zero
#define N64_TRIGGER_L 0x0020
#define N64_TRIGGER_R 0x0010
#define N64_BUTTON_C_UP 0x0008
#define N64_BUTTON_C_DOWN 0x0004
#define N64_BUTTON_C_LEFT 0x0002
#define N64_BUTTON_C_RIGHT 0x0001
// [16-8]: Analog X
//  [8-0]: Analog Y

bool HACK_GetN64Data(CSIDevice_N64Controller* dev, u32& data)
{
  GCPadStatus pad_status = dev->GetPadStatus();

  if (!pad_status.isConnected)
    return false;

  // TODO: Map this mess to N64 equivalents
  {
    // Translate buttons from GameCube to N64
    static const std::pair<u16, u16> table[] = {
        {PAD_BUTTON_LEFT, N64_BUTTON_LEFT},
        {PAD_BUTTON_RIGHT, N64_BUTTON_RIGHT},
        {PAD_BUTTON_DOWN, N64_BUTTON_DOWN},
        {PAD_BUTTON_UP, N64_BUTTON_UP},
        {PAD_TRIGGER_Z, N64_TRIGGER_Z},
        {PAD_TRIGGER_R, N64_TRIGGER_R},
        {PAD_TRIGGER_L, N64_TRIGGER_L},
        {PAD_BUTTON_A, N64_BUTTON_A},
        {PAD_BUTTON_B, N64_BUTTON_B},
        {PAD_BUTTON_START, N64_BUTTON_START},
        // temporary hack...
        {PAD_BUTTON_Y, N64_BUTTON_C_LEFT},
        {PAD_BUTTON_X, N64_BUTTON_C_DOWN},
    };

    u16 n64_buttons = 0;
    for (const auto& entry : table)
    {
      if (pad_status.button & entry.first)
        n64_buttons |= entry.second;
    }

    // TODO: The C buttons aren't handled yet...
    // as a temporary workaround, we could try mapping the C stick to the equivalent N64 buttons...
    // we could also map GameCube X and Y to the C-Down and C-Left buttons, respectively.

    // TODO: The analog stick is completely fucked.
    // Seems like they've already pre-normalized it, that only causes problems...

    s8 stickX = (s8)(pad_status.stickX - 128);
    s8 stickY = (s8)(pad_status.stickY - 128);

    // Using `low` because `high` has two reserved bits
    // TODO: This is wrong
    data = 0;
    data |= stickX << 0;
    data |= stickY << 8;
    data |= n64_buttons << 16;
  }
  return true;
}

int CSIDevice_N64Controller::RunBuffer(u8* buffer, int request_length)
{
  // For debug logging only
  ISIDevice::RunBuffer(buffer, request_length);

  const GCPadStatus pad_status = GetPadStatus();
  if (!pad_status.isConnected)
    return -1;

  // Read the command
  const auto command = static_cast<EBufferCommands>(buffer[0]);

  // Handle it
  switch (command)
  {
  case EBufferCommands::CMD_STATUS:
  case EBufferCommands::CMD_RESET:
  {
    const u32 id = Common::swap32(SI_N64_CONTROLLER | 0 /* No Pak installed */);
    std::memcpy(buffer, &id, 3);
    return 3;
  }

  case EBufferCommands::CMD_DIRECT_N64:
  {
    // N64 controllers only return 4 bytes, not 8, like this is doing.
    // Everything should be placed in the `low` byte, because a few bits of the `high` byte are reserved.
    // And, these reserved bits are used as controller inputs, which causes issues.
    //
    // TOOD: What is the return value used for?
    // And can we safely change it to 4?
    //
    // Or should we always return 8, regardless of this?
    // I think returning 8 is probably fine...
    //
    // Of course, the correct thing to do would be to confirm this behaviour based on a hardware test
    // Unfortunately, I do not own an N64 controller.
    //
    // I also haven't seen a single person use an N64 controller on a GameCube/Wii directly
    // Or someone using a GameCube controller on an N64...
    //
    // I've seen adapters that convert GameCube controllers to work on the N64, but those alter the protocol
    // which is not what we want - we want to use the native protocol, which means we need to write our own code
    // in order to correctly parse what the controller sends us.

    // Figured out how N64 pads work.
    // We must read the output from the SI_RAM buffer directly.
    // Only 4 bytes needed.
    // TODO: We must get rid of this GetData function (or at least greatly rework it)
    //
    // SI INPUT HIGH/LOW registers should not be used when reading data from N64 controllers.

    INFO_LOG_FMT(SERIALINTERFACE, "N64 - Direct (Request length: {})", request_length);
    u32 data;
    HACK_GetN64Data(this, data);
    for (int i = 0; i < 4; i++)
    {
      buffer[i] = (data >> (24 - (i * 8))) & 0xff;
    }
    return sizeof(data);
  }

  // not handling accessories yet...
  case EBufferCommands::CMD_READ_ACCESSORY_N64:
  case EBufferCommands::CMD_WRITE_ACCESSORY_N64:
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE, "Unknown SI command     ({:#x})", static_cast<u8>(command));
  }
  break;
  }

  return 0;
}


DataResponse CSIDevice_N64Controller::GetData(u32& hi, u32& low)
{
  return DataResponse::NoData;
}

void CSIDevice_N64Controller::SendCommand(u32 command, u8 poll)
{

}

}  // namespace SerialInterface
