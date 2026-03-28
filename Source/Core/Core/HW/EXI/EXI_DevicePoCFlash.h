// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>

#include "Common/BitUtils.h"
#include "Core/HW/EXI/EXI_Device.h"

class PointerWrap;

namespace ExpansionInterface
{
class CEXIPoCFlash : public IEXIDevice
{
public:
  explicit CEXIPoCFlash(Core::System& system);
  ~CEXIPoCFlash() override;

  void SetCS(int cs) override;
  bool IsPresent() const override;

private:
  std::unique_ptr<u8[]> m_flash;

  static constexpr auto FLASH_SIZE = 64 << 20;  // For now, use a 64 megabyte flash.
  static constexpr auto SECTOR_SIZE = 512;

  static constexpr u8 CHIP_ID[4] = { 0x1C, 0xDE, 0xEA, 0xDC };  // Randomly generated number

  enum eCommands
  {
    kCmd_ChipId       = 0,
    kCmd_ReadSector   = 1,
    kCmd_WriteSector  = 2,
    kCmd_EraseSector  = 3, // Fills the selected sector with 0xFFs
    kCmd_EraseFlash   = 4, // Fills the entire flash with 0xFFs
    kCmd_WriteEnable  = 5,
    kCmd_WriteDisable = 6,
    kCmd_EraseEnable  = 7,
    kCmd_EraseDisable = 8,
  };

  struct
  {
    u32 value;

    u32 command() const { return (value & 0xF0000000) >> 28; }
    u32 sector()  const { return (value & 0x0FFFF800) >> 11; }
    u32 unused()  const { return (value & 0x7FFF); } // Could be repurposed as a very simple checksum, currently unused.

    u32 byteOffset() const { return sector() * SECTOR_SIZE; }
  } m_command = { };

  u32 m_command_bytes_received = 0;
  u32 m_cursor = 0;

  bool m_can_erase = false;
  bool m_can_write = false;

  void TransferByte(u8& data) override;
};
}  // namespace ExpansionInterface
