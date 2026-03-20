// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI_Device.h"

namespace ExpansionInterface
{
class CEXIUSBAdapter final : public IEXIDevice
{
public:
  CEXIUSBAdapter(Core::System& system);

  void ImmWrite(u32 data, u32 size) override;
  u32 ImmRead(u32 size) override;
  void ImmReadWrite(u32& data, u32 size) override;

  void DMAWrite(u32 address, u32 size) override;
  void DMARead(u32 address, u32 size) override;

  bool IsPresent() const override;
  void SetCS(int cs) override;
  bool IsInterruptSet() override;

private:
  enum
  {
    kCmd_Id            = 0x0,
    kCmd_ReadRam       = 0x1,
    kCmd_ReadStatus    = 0x2,
    kCmd_ReadMailbox   = 0x3,
    // Command 4 is unknown (may not exist)
    kCmd_WriteRam      = 0x5,
    kCmd_WriteMailbox  = 0x6,
    // Command 7 is unknown (may not exist)
  };

  enum
  {
    kStatus_Tx = 0x01, // Tx mailbox interrupt (set when PC writes to the mailbox, GC should read it)
    kStatus_Rx = 0x02, // Rx mailbox interrupt (set when GC writes to the mailbox, PC should read it)
    kStatus_Id = 0x04, // ID interrupt (unknown)
  };

  bool m_first_write;
  u8 m_command;
  u16 m_address;

  u32 m_mailbox;
  u32 m_status;

  std::unique_ptr<u8[]> m_ram;
};
}  // namespace ExpansionInterface
