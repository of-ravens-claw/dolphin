// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceWaikiki.h"

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "Common/Thread.h"
#include "Core/Core.h"

namespace ExpansionInterface
{
u16 WaikikiSockServer::server_port;
int WaikikiSockServer::client_count;
std::thread WaikikiSockServer::connectionThread;
Common::Flag WaikikiSockServer::server_running;
std::mutex WaikikiSockServer::connection_lock;
std::queue<std::unique_ptr<sf::TcpSocket>> WaikikiSockServer::waiting_socks;

WaikikiSockServer::WaikikiSockServer(volatile bool* interrupt) : client_running(false), interrupt_state(interrupt)
{
  if (!connectionThread.joinable())
    connectionThread = std::thread(WaikikiConnectionWaiter);
}

WaikikiSockServer::~WaikikiSockServer()
{
  if (clientThread.joinable())
  {
    --client_count;

    client_running.Clear();
    clientThread.join();
  }

  if (client_count <= 0 && connectionThread.joinable())
  {
    server_running.Clear();
    connectionThread.join();
  }
}

void WaikikiSockServer::WaikikiConnectionWaiter()
{
  Common::SetCurrentThreadName("Waikiki Connection Waiter");

  sf::TcpListener server;
  server_port = 0xda22; // 55842
  for (int bind_tries = 0; bind_tries <= 10 && !server_running.IsSet(); bind_tries++)
  {
    server_running.Set(server.listen(server_port) == sf::Socket::Status::Done);
    if (!server_running.IsSet())
      server_port++;
  }

  if (!server_running.IsSet())
    return;

  Core::DisplayMessage(fmt::format("Waikiki: Listening on TCP port {}", server_port), 5000);

  server.setBlocking(false);

  auto new_client = std::make_unique<sf::TcpSocket>();
  while (server_running.IsSet())
  {
    if (server.accept(*new_client) == sf::Socket::Status::Done)
    {
      std::lock_guard lk(connection_lock);
      waiting_socks.push(std::move(new_client));

      new_client = std::make_unique<sf::TcpSocket>();
    }

    Common::SleepCurrentThread(1);
  }
}

bool WaikikiSockServer::GetAvailableSock()
{
  bool sock_filled = false;

  std::lock_guard lk(connection_lock);

  if (!waiting_socks.empty())
  {
    client = std::move(waiting_socks.front());
    if (clientThread.joinable())
    {
      client_running.Clear();
      clientThread.join();

      recv_fifo = std::deque<u8>();
      send_fifo = std::deque<u8>();
    }
    clientThread = std::thread(&WaikikiSockServer::ClientThread, this);
    client_count++;
    waiting_socks.pop();
    sock_filled = true;
  }

  return sock_filled;
}

void WaikikiSockServer::ClientThread()
{
  client_running.Set();

  Common::SetCurrentThreadName("Waikiki Client");

  client->setBlocking(false);

  while (client_running.IsSet())
  {
    bool did_nothing = true;

    {
      std::lock_guard lk(transfer_lock);

      // what's an ideal buffer size?
      std::array<char, 128> buffer;
      std::size_t got = 0;

      if (client->receive(buffer.data(), buffer.size(), got) == sf::Socket::Status::Disconnected)
        client_running.Clear();

      if (got != 0)
      {
        did_nothing = false;

        recv_fifo.insert(recv_fifo.end(), buffer.data(), &buffer[got]);
        *interrupt_state = true;
      }
      else
      {
        *interrupt_state = false;
      }

      if (!send_fifo.empty())
      {
        did_nothing = false;

        std::vector<char> packet(send_fifo.begin(), send_fifo.end());
        send_fifo.clear();

        if (client->send(&packet[0], packet.size()) == sf::Socket::Status::Disconnected)
          client_running.Clear();
      }
    }  // unlock transfer

    if (did_nothing)
      Common::YieldCPU();
  }

  client->disconnect();
}

CEXIWaikiki::CEXIWaikiki(Core::System& system) : IEXIDevice(system), WaikikiSockServer(&m_interrupt_state)
{
}

void CEXIWaikiki::SetCS(int cs)
{
  if (cs)
    m_position = 0;
}

bool CEXIWaikiki::IsPresent() const
{
  return true;
}

// Waikiki documentation:
// Runs at 32 MHz
//
// Waikiki buffers (it's a FIFO):
// RECV - GameCube out, PC in (GC -> WK -> PC)
// SEND - GameCube in, PC out (PC -> WK -> GC)
// .... i am convinced that these names are confusing because I have confused myself.
// I do not know which one is used for what anymore ... fucking hell
//
// Minimum buffer length before the SDK will send data (to PC) is 12 bytes.
// ??? It should also be a multiple of 4 bytes ???
// Maximum length is ??? (probably infinite...)
// real waikiki hw length is probably 16 or 32, as those are common sizes for FIFOs
//
// Waikiki commands:
//
// 0x00 (init) - Standard EXI init command, Waikiki ID is 0x070200XX, where XX is the version.
// 0x20 (?) - Sent before `checkBuf`
// 0x80 (checkBuf) - Gets the size of the RECV and SEND buffers.
// 0x88 (read) - Reads data from the RECV buffer. (PC -> GC)
// 0xF0 (write) - Writes data into the SEND buffer. (GC -> PC)
//
// the following may or may not exist.
// 0xC0 - enable interrupt
// 0xC1 - disable interrupt
// 0xC2 - clear interrupt

void CEXIWaikiki::TransferByte(u8& byte)
{
  if (!client || client->getLocalPort() == 0)
    GetAvailableSock();

  if (m_position == 0)
  {
    m_command = byte;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "Waikiki command: 0x{:02X}", m_command);
  }
  else
  {
    switch (m_command)
    {
    case init:
    {
      m_ad16_register.U32 = Common::swap32(0x07020001);
      switch (m_position)
      {
      case 2:
        byte = m_ad16_register.U8[0];
        break;
      case 3:
        byte = m_ad16_register.U8[1];
        break;
      case 4:
        byte = m_ad16_register.U8[2];
        break;
      case 5:
        byte = m_ad16_register.U8[3];
        break;
      }
    }
    break;

    case checkBuf:
    {
      std::lock_guard lk(transfer_lock);

      m_ad16_register.U16[0] = Common::swap16(send_fifo.empty() ? 12 : 0); // send
      m_ad16_register.U16[1] = Common::swap16(recv_fifo.empty() ? 0 : (u16)recv_fifo.size()); // recv

      switch (m_position)
      {
      case 2:
        byte = m_ad16_register.U8[0];
        break;
      case 3:
        byte = m_ad16_register.U8[1];
        break;
      case 4:
        byte = m_ad16_register.U8[2];
        break;
      case 5:
        byte = m_ad16_register.U8[3];
        DEBUG_LOG_FMT(EXPANSIONINTERFACE, "Waikiki checkBuf: 0x{:08X} ([0]:{} [1]:{})",
                      m_ad16_register.U32, Common::swap16(m_ad16_register.U16[0]), Common::swap16(m_ad16_register.U16[1]));
        break;
      }
    }
    break;

    case read:
    {
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "Waikiki read: pos {}", m_position);
      //byte = 0; // stubbed...

      std::lock_guard lk(transfer_lock);
      if (!recv_fifo.empty())
      {
        byte = recv_fifo.front();
        recv_fifo.pop_front();
      }
    }
    break;

    case write:
    {
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "Waikiki write: pos {}", m_position);

      // ignore it...
      if (m_position == 1)
        break;

      std::lock_guard lk(transfer_lock);
      send_fifo.push_back(byte);
    }
    break;

    case 0xC0:
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE, "Waikiki: enable interrupt");
      m_interrupt_enabled = true;
      byte = 0xFF;  // ?
    }
    break;

    case 0xC1:
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE, "Waikiki: disable interrupt");
      m_interrupt_enabled = false;
      byte = 0xFF;  // ?
    }
    break;

    case 0xC2:
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE, "Waikiki: clear interrupt");
      m_interrupt_state = false;
      byte = 0xFF; // ?
    }
    break;
    }
  }

  m_position++;
}

void CEXIWaikiki::DoState(PointerWrap& p)
{
  p.Do(m_position);
  p.Do(m_command);
  p.Do(m_ad16_register);
  p.Do(m_interrupt_enabled);
  p.Do(m_interrupt_state);
}

bool CEXIWaikiki::IsInterruptSet()
{
  if (m_interrupt_enabled)
    return m_interrupt_state;

  return false;
}

}  // namespace ExpansionInterface
