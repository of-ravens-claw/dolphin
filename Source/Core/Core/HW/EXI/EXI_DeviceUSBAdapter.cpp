// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceUSBAdapter.h"

#include <string>

#include "Core/System.h"
#include "Core/HW/Memmap.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace ExpansionInterface
{
CEXIUSBAdapter::CEXIUSBAdapter(Core::System& system) : IEXIDevice(system)
{
  m_ram = std::make_unique<u8[]>(0x20000); // 128 KB
  m_first_write = false;
  m_command = 0;
  m_address = 0;
  m_mailbox = 0;
  m_status  = 0;
}

void CEXIUSBAdapter::ImmWrite(u32 data, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmWrite: {:08x}", data);

  if (m_first_write)
  {
    m_command = (data >> 29) & 0x07;

    if (m_command == kCmd_ReadRam || m_command == kCmd_WriteRam)
      m_address = (data >> 10) & 0x7FFF;

    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmWrite got command {:x}, address {:x}",
                 m_command, m_address << 2);

    m_first_write = false;
  }
  else
  {
    switch (m_command)
    {
    case kCmd_WriteMailbox:
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmRead: Writing mailbox {:08x}", data);
      m_mailbox = data & 0x1FFFFFFF;
      m_status |= kStatus_Rx;
      break;

    default:
      break;
    }
  }
}

u32 CEXIUSBAdapter::ImmRead(u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmRead");

  switch (m_command)
  {
  case kCmd_Id:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmRead: Returning ID 0x01010000");
    return 0x01010000;

  case kCmd_ReadStatus:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmRead: Reading status {:08x}", m_status);
    return m_status;

  case kCmd_ReadMailbox:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmRead: Reading mailbox {:08x}", m_mailbox);
    return m_mailbox;

  default:
    return 0;
  }
}

void CEXIUSBAdapter::ImmReadWrite(u32& data, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER ImmReadWrite: {:08x}", data);
  ImmWrite(data, size);
  data = ImmRead(size);
}

void CEXIUSBAdapter::DMAWrite(u32 address, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER DMAWrite: {:08x} bytes, from {:08x} to device", size,
               address);

  auto& memory = m_system.GetMemory();
  memory.CopyFromEmu(&m_ram[m_address << 2], address, size);
}

void CEXIUSBAdapter::DMARead(u32 address, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER DMARead:  {:08x} bytes, from device to {:08x}", size,
               address);

  auto& memory = m_system.GetMemory();
  memory.CopyToEmu(address, &m_ram[m_address << 2], size);
}

bool CEXIUSBAdapter::IsPresent() const
{
  return true;
}

void CEXIUSBAdapter::SetCS(int cs)
{
  if (!cs)
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER CS set HIGH (end xfer)");
  else
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI USB-ADAPTER CS set LOW (start xfer)");

  if (cs)
  {
    m_command = 0;
    m_address = 0;
    m_first_write = true;
  }
}

bool CEXIUSBAdapter::IsInterruptSet()
{
  // This interrupt should trigger when the PC writes to the mailbox (Tx)
  return m_status & kStatus_Tx;
}
}  // namespace ExpansionInterface
