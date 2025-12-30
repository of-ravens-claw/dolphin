// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/EXI/EXI_Device.h"

#include <SFML/Network.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "Common/Flag.h"

class PointerWrap;

namespace ExpansionInterface
{
class WaikikiSockServer
{
public:
  WaikikiSockServer(volatile bool* interrupt);
  ~WaikikiSockServer();
  bool GetAvailableSock();

  // Client for this server object
  std::unique_ptr<sf::TcpSocket> client;
  void ClientThread();
  std::thread clientThread;
  std::mutex transfer_lock;

  std::deque<u8> send_fifo;
  std::deque<u8> recv_fifo;

private:
  static int client_count;
  Common::Flag client_running;

  volatile bool* interrupt_state = nullptr;

  // Only ever one server thread
  static void WaikikiConnectionWaiter();

  static u16 server_port;
  static Common::Flag server_running;
  static std::thread connectionThread;
  static std::mutex connection_lock;
  static std::queue<std::unique_ptr<sf::TcpSocket>> waiting_socks;
};

class CEXIWaikiki : public IEXIDevice, private WaikikiSockServer
{
public:
  explicit CEXIWaikiki(Core::System& system);
  void SetCS(int cs) override;
  bool IsPresent() const override;
  void DoState(PointerWrap& p) override;

  bool IsInterruptSet() override;

private:
  enum
  {
    init = 0x00,
    unk_20 = 0x20,
    checkBuf = 0x80,
    read = 0x88,
    unk_A0 = 0xA0,
    write = 0xF0,
  };

  union AD16Reg
  {
    u32 U32 = 0;
    u16 U16[2];
    u8 U8[4];
  };

  // STATE_TO_SAVE
  u32 m_position = 0;
  u32 m_command = 0;
  AD16Reg m_ad16_register;
  bool m_interrupt_enabled = false; // interrupt on/off, determines if it will fire.
  volatile bool m_interrupt_state = false; // actual state of the interrupt

  void TransferByte(u8& byte) override;
};
}  // namespace ExpansionInterface
