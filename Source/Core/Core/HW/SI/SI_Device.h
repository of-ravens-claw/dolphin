// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include "Common/CommonTypes.h"

class PointerWrap;
namespace Core
{
class System;
}
namespace SystemTimers
{
class SystemTimersManager;
}

namespace SerialInterface
{
// Devices can reply with these
constexpr u32 SI_ERROR_NO_RESPONSE = 0x0008;  // Nothing is attached
constexpr u32 SI_ERROR_UNKNOWN     = 0x0040;  // Unknown device is attached
constexpr u32 SI_ERROR_BUSY        = 0x0080;  // Still detecting

// Device types
constexpr u32 SI_TYPE_MASK = 0x1800'0000;
constexpr u32 SI_TYPE_N64  = 0x0000'0000;
constexpr u32 SI_TYPE_GC   = 0x0800'0000;
constexpr u32 SI_TYPE_GCAM = 0x1000'0000; // Made up by me, Triforce baseboard uses this bit...
// GCAM = GameCube Arcade Machine (official Nintendo name)

// GC Controller types
constexpr u32 SI_GC_WIRELESS = 0x8000'0000;
constexpr u32 SI_GC_NOMOTOR  = 0x2000'0000;  // No rumble motor
constexpr u32 SI_GC_STANDARD = 0x0100'0000;

// WaveBird specific
constexpr u32 SI_WIRELESS_RECEIVED  = 0x40000000; // 0: no wireless unit
constexpr u32 SI_WIRELESS_IR        = 0x04000000; // 0: IR  1: RF
constexpr u32 SI_WIRELESS_STATE     = 0x02000000; // 0: variable  1: fixed
constexpr u32 SI_WIRELESS_ORIGIN    = 0x00200000; // 0: invalid  1: valid
constexpr u32 SI_WIRELESS_FIX_ID    = 0x00100000; // 0: not fixed  1: fixed
constexpr u32 SI_WIRELESS_TYPE      = 0x000F0000;
constexpr u32 SI_WIRELESS_LITE_MASK = 0x000C0000; // 0: normal 1: lite controller
constexpr u32 SI_WIRELESS_LITE      = 0x00040000; // 0: normal 1: lite controller
constexpr u32 SI_WIRELESS_CONT_MASK = 0x00080000; // 0: non-controller 1: non-controller
constexpr u32 SI_WIRELESS_CONT      = 0x00000000;
constexpr u32 SI_WIRELESS_ID        = 0x00C0FF00;
constexpr u32 SI_WIRELESS_TYPE_ID   = (SI_WIRELESS_TYPE | SI_WIRELESS_ID);

// SI Device IDs for emulator use
enum TSIDevices : u32
{
  SI_N64_MIC         = (SI_TYPE_N64  | 0x0001'0000),
  SI_N64_KEYBOARD    = (SI_TYPE_N64  | 0x0002'0000),
  SI_N64_MOUSE       = (SI_TYPE_N64  | 0x0200'0000),
  SI_N64_CONTROLLER  = (SI_TYPE_N64  | 0x0500'0000),
  SI_GBA             = (SI_TYPE_N64  | 0x0004'0000),
  SI_GC_CONTROLLER   = (SI_TYPE_GC   | SI_GC_STANDARD),
  SI_GC_KEYBOARD     = (SI_TYPE_GC   | 0x0020'0000),
  SI_GC_STEERING     = (SI_TYPE_GC   | 0x0000'0000),
  SI_GC_RECEIVER     = (SI_TYPE_GC   | SI_GC_WIRELESS),
  SI_GC_WAVEBIRD     = (SI_TYPE_GC   | SI_GC_WIRELESS | SI_GC_STANDARD | SI_WIRELESS_STATE | SI_WIRELESS_FIX_ID),
  SI_AM_BASEBOARD    = (SI_TYPE_GCAM | 0x00110800), // gets ORd with dipswitch state
};

// Commands
enum class EBufferCommands : u8
{
  // TODO: There are more commands, the current peripherals don't implement all supported commands.
  // For example, the Keyboard has at least 2 read commands.
  // And the GameCube controller has a second read command with more data.
  CMD_STATUS = 0x00,
  CMD_DIRECT_N64 = 0x01,
  CMD_READ_ACCESSORY_N64 = 0x02,
  CMD_WRITE_ACCESSORY_N64 = 0x03,
  CMD_READ_GBA = 0x14,
  CMD_WRITE_GBA = 0x15,
  CMD_SET_GAME_ID = 0x1d,
  CMD_DIRECT_STEERING = 0x30,
  CMD_DIRECT = 0x40,
  CMD_ORIGIN = 0x41,
  CMD_RECALIBRATE = 0x42,
  CMD_DIRECT_LONG = 0x43,
  CMD_DIRECT_KB = 0x54,
  CMD_RESET = 0xFF
};

// For configuration use, since some devices can have the same SI Device ID
enum SIDevices : int
{
  SIDEVICE_NONE,
  SIDEVICE_N64_MIC,
  SIDEVICE_N64_KEYBOARD,
  SIDEVICE_N64_MOUSE,
  SIDEVICE_N64_CONTROLLER,
  SIDEVICE_GC_GBA,
  SIDEVICE_GC_CONTROLLER,
  SIDEVICE_GC_KEYBOARD,
  SIDEVICE_GC_STEERING,
  SIDEVICE_DANCEMAT,
  SIDEVICE_GC_TARUKONGA,
  // Was used for Triforce in the past, but the implementation is no longer in Dolphin.
  // It's kept here so that values below will stay constant.
  SIDEVICE_AM_BASEBOARD,
  SIDEVICE_WIIU_ADAPTER,
  SIDEVICE_GC_GBA_EMULATED,
  SIDEVICE_DEBUG_UART, // Simple UART. Currently TX only (GC -> PC)
  // Not a valid device. Used for checking whether enum values are valid.
  SIDEVICE_COUNT,
};

enum class DataResponse
{
  NoData,
  Success,
  ErrorNoResponse,
};

class ISIDevice
{
public:
  ISIDevice(Core::System& system, SIDevices device_type, int device_number);
  virtual ~ISIDevice();

  int GetDeviceNumber() const;
  SIDevices GetDeviceType() const;

  // Run the SI Buffer
  // Return value:
  //  positive: The response length.
  //  0: Response not ready, we will try again `TransferInterval()` cycles later.
  //  -1: No response.
  virtual int RunBuffer(u8* buffer, int request_length);

  virtual int TransferInterval();

  virtual DataResponse GetData(u32& hi, u32& low) = 0;

  // Send a command directly (no detour per buffer)
  virtual void SendCommand(u32 command, u8 poll) = 0;

  // Savestate support
  virtual void DoState(PointerWrap& p);

  // Schedulable event
  virtual void OnEvent(u64 userdata, s64 cycles_late);

protected:
  // Only the three high bytes of `si_device_id` are used.
  static int CreateStatusResponse(u32 si_device_id, u8* buffer);

  Core::System& m_system;

  int m_device_number;
  SIDevices m_device_type;
};

int SIDevice_GetGBATransferTime(const SystemTimers::SystemTimersManager& timers,
                                EBufferCommands cmd);
bool SIDevice_IsGCController(SIDevices type);

std::unique_ptr<ISIDevice> SIDevice_Create(Core::System& system, SIDevices device, int port_number);
}  // namespace SerialInterface
