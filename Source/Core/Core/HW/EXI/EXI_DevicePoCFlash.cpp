// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DevicePoCFlash.h"

#include <cstring>
#include <string>

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Swap.h"
#include "Common/Timer.h"

#include "Core/Config/MainSettings.h"
#include "Core/Config/SessionSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Sram.h"
#include "Core/HW/SystemTimers.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/System.h"

#include "DiscIO/Enums.h"

namespace ExpansionInterface
{

CEXIPoCFlash::CEXIPoCFlash(Core::System& system) : IEXIDevice(system)
{
  m_flash = std::make_unique<u8[]>(FLASH_SIZE);

  // Start with the chip erased.
  std::memset(m_flash.get(), 0xFF, FLASH_SIZE);

  static const auto FLASH_FILENAME = File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + "flash.raw";

  // If a saved Flash file exists, we should load it here:
  File::IOFile stream(FLASH_FILENAME, "rb");
  if (!stream)
    return;

  const u64 filesize = stream.GetSize();
  if (!stream.ReadBytes(m_flash.get(), std::min<u64>(filesize, FLASH_SIZE)))
    return;
}

CEXIPoCFlash::~CEXIPoCFlash()
{
  static const auto FLASH_FILENAME = File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + "flash.raw";

  // Flush the Flash to a file
  File::IOFile stream(FLASH_FILENAME, "wb+");
  if (!stream)
    return;

  if (!stream.WriteBytes(m_flash.get(), FLASH_SIZE))
    return;
}

void CEXIPoCFlash::SetCS(int cs)
{
  if (cs)
  {
    m_command_bytes_received = 0;
    m_cursor = 0;
  }
}

bool CEXIPoCFlash::IsPresent() const
{
  return true;
}

void CEXIPoCFlash::TransferByte(u8& data)
{
  // The first 4 bytes must be the command
  // If we haven't read it, do it now
  if (m_command_bytes_received < sizeof(m_command))
  {
    m_command.value <<= 8;
    m_command.value |= data;
    data = 0xFF; // leave the bus floating
    m_command_bytes_received++;

    if (m_command_bytes_received == sizeof(m_command))
    {
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash cmd {} {:06x}", m_command.command(),
                    m_command.sector());
    }
    return;
  }

  const eCommands command = static_cast<eCommands>(m_command.command());

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash data {} {:06x} {:02x}", m_command.command(),
                m_command.sector(), data);

  switch (command)
  {
  case kCmd_ChipId:
    data = CHIP_ID[m_cursor];
    break;

  case kCmd_ReadSector:
    data = m_flash[m_command.byteOffset() + m_cursor];
    break;

  case kCmd_WriteSector:
    if (m_can_write)
      m_flash[m_command.byteOffset() + m_cursor] = data;
    break;

  case kCmd_EraseSector:
    if (m_can_erase)
      std::memset(m_flash.get() + m_command.byteOffset(), 0xFF, SECTOR_SIZE);
    break;

  case kCmd_EraseFlash:
    if (m_can_erase)
      std::memset(m_flash.get(), 0xFF, FLASH_SIZE);
    break;

  case kCmd_WriteEnable:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash write commands enabled");
    m_can_write = true;
    break;

  case kCmd_WriteDisable:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash write commands disabled");
    m_can_write = false;
    break;

  case kCmd_EraseEnable:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash erase commands enabled");
    m_can_erase = true;
    break;

  case kCmd_EraseDisable:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash erase commands disabled");
    m_can_erase = false;
    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "PoC-Flash unhandled command {}", m_command.command());
    break;
  }

  // This must be done last!
  m_cursor++;
}
}  // namespace ExpansionInterface
