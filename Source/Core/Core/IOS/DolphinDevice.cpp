// Copyright 2019 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/DolphinDevice.h"

#include <algorithm>
#include <cstring>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/SettingsHandler.h"
#include "Common/Timer.h"
#include "Common/Version.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/VideoConfig.h"

namespace IOS::HLE
{
namespace
{
enum
{
  IOCTL_DOLPHIN_GET_ELAPSED_TIME = 0x01,
  IOCTL_DOLPHIN_GET_VERSION = 0x02,
  IOCTL_DOLPHIN_GET_SPEED_LIMIT = 0x03,
  IOCTL_DOLPHIN_SET_SPEED_LIMIT = 0x04,
  IOCTL_DOLPHIN_GET_CPU_SPEED = 0x05,
  IOCTL_DOLPHIN_GET_REAL_PRODUCTCODE = 0x06,
  IOCTL_DOLPHIN_DISCORD_SET_CLIENT = 0x07,
  IOCTL_DOLPHIN_DISCORD_SET_PRESENCE = 0x08,
  IOCTL_DOLPHIN_DISCORD_RESET = 0x09,
  IOCTL_DOLPHIN_GET_SYSTEM_TIME = 0x0A,

  // Extensions, not available in normal builds of Dolphin.
  // We use IDs starting at 0x100 to make sure it won't conflict in case they add new commands.
  IOCTL_DOLPHIN_ARE_EXTENSIONS_AVAILABLE = 0x100,
  IOCTL_DOLPHIN_GET_ASPECT_RATIO = 0x101,
  IOCTL_DOLPHIN_GET_WINDOW_RESOLUTION = 0x102,
  IOCTL_DOLPHIN_GET_MOUSE_DATA = 0x103,
  // Maybe implement mouse access here?
  // And also maybe a backdoor to read/write files anywhere on the host
  // though that could be easily abused...

  // Backdoor - provides functions to Open, Read, and Write a file directly on the host file system.
  // No hand holding.
  IOCTL_BACKDOOR_OPEN_FILE  = 0x201, // fopen
  IOCTL_BACKDOOR_CLOSE_FILE = 0x202, // fclose
  IOCTL_BACKDOOR_TELL_FILE  = 0x203, // ftell
  IOCTL_BACKDOOR_SEEK_FILE  = 0x204, // fseek
  IOCTL_BACKDOOR_READ_FILE  = 0x205, // fread
  IOCTL_BACKDOOR_WRITE_FILE = 0x206, // fwrite

};

IPCReply GetVersion(Core::System& system, const IOCtlVRequest& request)
{
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  const auto length = std::min(size_t(request.io_vectors[0].size), Common::GetScmDescStr().size());

  auto& memory = system.GetMemory();
  memory.Memset(request.io_vectors[0].address, 0, request.io_vectors[0].size);
  memory.CopyToEmu(request.io_vectors[0].address, Common::GetScmDescStr().data(), length);

  return IPCReply(IPC_SUCCESS);
}

IPCReply GetCPUSpeed(Core::System& system, const IOCtlVRequest& request)
{
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  if (request.io_vectors[0].size != 4)
  {
    return IPCReply(IPC_EINVAL);
  }

  const bool oc = system.GetCoreTiming().GetOverclock();
  const u32 core_clock = u32(float(system.GetSystemTimers().GetTicksPerSecond()) * oc);

  auto& memory = system.GetMemory();
  memory.Write_U32(core_clock, request.io_vectors[0].address);

  return IPCReply(IPC_SUCCESS);
}

IPCReply GetSpeedLimit(Core::System& system, const IOCtlVRequest& request)
{
  // get current speed limit
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  if (request.io_vectors[0].size != 4)
  {
    return IPCReply(IPC_EINVAL);
  }

  const u32 speed_percent = Config::Get(Config::MAIN_EMULATION_SPEED) * 100;

  auto& memory = system.GetMemory();
  memory.Write_U32(speed_percent, request.io_vectors[0].address);

  return IPCReply(IPC_SUCCESS);
}

IPCReply SetSpeedLimit(Core::System& system, const IOCtlVRequest& request)
{
  // set current speed limit
  if (!request.HasNumberOfValidVectors(1, 0))
  {
    return IPCReply(IPC_EINVAL);
  }

  if (request.in_vectors[0].size != 4)
  {
    return IPCReply(IPC_EINVAL);
  }

  auto& memory = system.GetMemory();
  const float speed = float(memory.Read_U32(request.in_vectors[0].address)) / 100.0f;
  Config::SetCurrent(Config::MAIN_EMULATION_SPEED, speed);

  return IPCReply(IPC_SUCCESS);
}

IPCReply GetRealProductCode(Core::System& system, const IOCtlVRequest& request)
{
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  const std::string backup_file_path = File::GetUserPath(D_BACKUP_IDX) + DIR_SEP + WII_SETTING;

  File::IOFile file(backup_file_path, "rb");
  if (!file)
    return IPCReply(IPC_ENOENT);

  Common::SettingsBuffer data;

  if (!file.ReadBytes(data.data(), data.size()))
    return IPCReply(IPC_ENOENT);

  const Common::SettingsReader settings_reader(data);
  const std::string code = settings_reader.GetValue("CODE");

  const size_t length = std::min<size_t>(request.io_vectors[0].size, code.length());
  if (length == 0)
    return IPCReply(IPC_ENOENT);

  auto& memory = system.GetMemory();
  memory.Memset(request.io_vectors[0].address, 0, request.io_vectors[0].size);
  memory.CopyToEmu(request.io_vectors[0].address, code.c_str(), length);
  return IPCReply(IPC_SUCCESS);
}

IPCReply SetDiscordClient(Core::System& system, const IOCtlVRequest& request)
{
  if (!Config::Get(Config::MAIN_USE_DISCORD_PRESENCE))
    return IPCReply(IPC_EACCES);

  if (!request.HasNumberOfValidVectors(1, 0))
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  std::string new_client_id =
      memory.GetString(request.in_vectors[0].address, request.in_vectors[0].size);

  Host_UpdateDiscordClientID(new_client_id);

  return IPCReply(IPC_SUCCESS);
}

IPCReply SetDiscordPresence(Core::System& system, const IOCtlVRequest& request)
{
  if (!Config::Get(Config::MAIN_USE_DISCORD_PRESENCE))
    return IPCReply(IPC_EACCES);

  if (!request.HasNumberOfValidVectors(10, 0))
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();

  std::string details = memory.GetString(request.in_vectors[0].address, request.in_vectors[0].size);
  std::string state = memory.GetString(request.in_vectors[1].address, request.in_vectors[1].size);
  std::string large_image_key =
      memory.GetString(request.in_vectors[2].address, request.in_vectors[2].size);
  std::string large_image_text =
      memory.GetString(request.in_vectors[3].address, request.in_vectors[3].size);
  std::string small_image_key =
      memory.GetString(request.in_vectors[4].address, request.in_vectors[4].size);
  std::string small_image_text =
      memory.GetString(request.in_vectors[5].address, request.in_vectors[5].size);

  int64_t start_timestamp = memory.Read_U64(request.in_vectors[6].address);
  int64_t end_timestamp = memory.Read_U64(request.in_vectors[7].address);
  int party_size = memory.Read_U32(request.in_vectors[8].address);
  int party_max = memory.Read_U32(request.in_vectors[9].address);

  bool ret = Host_UpdateDiscordPresenceRaw(details, state, large_image_key, large_image_text,
                                           small_image_key, small_image_text, start_timestamp,
                                           end_timestamp, party_size, party_max);

  if (!ret)
    return IPCReply(IPC_EACCES);

  return IPCReply(IPC_SUCCESS);
}

IPCReply ResetDiscord(const IOCtlVRequest& request)
{
  if (!Config::Get(Config::MAIN_USE_DISCORD_PRESENCE))
    return IPCReply(IPC_EACCES);

  Host_UpdateDiscordClientID();

  return IPCReply(IPC_SUCCESS);
}

// Our extensions start here

// In our custom builds, this will return IPC_SUCCESS.
// In normal/master builds, this will return IPC_EINVAL.
IPCReply AreExtensionsAvailable(const IOCtlVRequest& request)
{
  return IPCReply(IPC_SUCCESS);
}

IPCReply GetAspectRatio(Core::System& system, const IOCtlVRequest& request)
{
  // TODO: Maybe we should just return the aspect ratio float instead
  // I don't know.

  // Expects 3 32-bit integers, in the following order:
  // [0]: Aspect Ratio Mode (enum)
  // [1]: Aspect Ratio Width
  // [2]: Aspect Ratio Height
  if (!request.HasNumberOfValidVectors(0, 3))
    return IPCReply(IPC_EINVAL);

  if (request.io_vectors[0].size != 4 || request.io_vectors[1].size != 4 ||
      request.io_vectors[2].size != 4)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  AspectMode mode = Config::GFX_ASPECT_RATIO.GetCachedValue().value;

  u32 width, height;

  if (mode == AspectMode::Custom || mode == AspectMode::CustomStretch)
  {
    width = Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH.GetCachedValue().value;
    height = Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT.GetCachedValue().value;
  }
  else
  {
    width = 0;
    height = 0;
  }

  memory.CopyToEmu(request.io_vectors[0].address, &mode, request.io_vectors[0].size);
  memory.CopyToEmu(request.io_vectors[1].address, &width, request.io_vectors[1].size);
  memory.CopyToEmu(request.io_vectors[2].address, &height, request.io_vectors[2].size);

  return IPCReply(IPC_SUCCESS);
}

IPCReply GetWindowResolution(Core::System& system, const IOCtlVRequest& request)
{
  return IPCReply(IPC_EINVAL); // not implemented, return an error.
}

IPCReply GetMouseData(Core::System& system, const IOCtlVRequest& request)
{
  return IPCReply(IPC_EINVAL);  // not implemented, return an error.
}

static_assert(sizeof(u64) == sizeof(void*),
              "This must be compiled in a system where the pointer size is 8 bytes");

IPCReply Backdoor_OpenFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File path, char[] buffer
  // IN 1 - Open mode, 0 - Read, 1 - Write
  // IO 0 - [out] file handle (host pointer, treat as a blackbox)
  if (!request.HasNumberOfValidVectors(2, 1))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[1].size != 4 || request.io_vectors[0].size != 8)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  std::string file_path =
      memory.GetString(request.in_vectors[0].address, request.in_vectors[0].size);

  int open_mode = memory.Read_U32(request.in_vectors[1].address);
  const char* libc_open_mode = nullptr;
  if (open_mode == 0)
    libc_open_mode = "rb";
  else if (open_mode == 1)
    libc_open_mode = "wb";
  else
    ASSERT_MSG(IOS, false, "Invalid open mode passed to Backdoor_OpenFile");

  FILE* fp = fopen(file_path.c_str(), libc_open_mode);
  if (fp == nullptr)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_OpenFile -> fopen failed, errno {}", errno);
    return IPCReply(IPC_EINVAL);
  }

  memory.Write_U64((u64)fp, request.io_vectors[0].address);
  return IPCReply(IPC_SUCCESS);
}

IPCReply Backdoor_CloseFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File handle
  if (!request.HasNumberOfValidVectors(1, 0))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[0].size != 8)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  u64 fp_uptr = memory.Read_U64(request.in_vectors[0].address);

  if (fp_uptr == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_CloseFile -> Invalid handle");
    return IPCReply(IPC_EINVAL);
  }

  int ret = fclose(reinterpret_cast<FILE*>(fp_uptr));
  if (ret != 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_CloseFile -> fclose failed, errno {}", errno);
    return IPCReply(IPC_EINVAL);
  }

  return IPCReply(IPC_SUCCESS);
}

IPCReply Backdoor_TellFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File handle
  // IO 0 - Return value (current file pointer)
  if (!request.HasNumberOfValidVectors(1, 1))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[0].size != 8 || request.io_vectors[0].size != 4)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  u64 fp_uptr = memory.Read_U64(request.in_vectors[0].address);

  if (fp_uptr == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_TellFile -> Invalid handle");
    return IPCReply(IPC_EINVAL);
  }

  auto ret = ftell(reinterpret_cast<FILE*>(fp_uptr));
  if (ret == -1)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_TellFile -> ftell failed, errno {}", errno);
    return IPCReply(IPC_EINVAL);
  }

  memory.Write_U32(ret, request.io_vectors[0].address);
  return IPCReply(IPC_SUCCESS);
}

IPCReply Backdoor_SeekFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File handle
  // IN 1 - Seek offset
  // IN 2 - Seek origin, 0 = SEEK_SET, 1 = SEEK_CUR, 2 = SEEK_END
  if (!request.HasNumberOfValidVectors(3, 0))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[0].size != 8 || request.in_vectors[1].size != 4 ||
      request.in_vectors[2].size != 4)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  u64 fp_uptr = memory.Read_U64(request.in_vectors[0].address);

  if (fp_uptr == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_SeekFile -> Invalid handle");
    return IPCReply(IPC_EINVAL);
  }

  int seek_offset = memory.Read_U32(request.in_vectors[1].address);
  int seek_origin = memory.Read_U32(request.in_vectors[2].address);
  int libc_origin = -1; // To be safe, we must translate it.
  switch (seek_origin)
  {
  case 0:
    libc_origin = SEEK_SET;
    break;
  case 1:
    libc_origin = SEEK_CUR;
    break;
  case 2:
    libc_origin = SEEK_END;
    break;
  default:
    ASSERT_MSG(IOS, false, "Invalid seek origin passed to Backdoor_SeekFile");
    break;
  }

  int ret = fseek(reinterpret_cast<FILE*>(fp_uptr), seek_offset, libc_origin);
  if (ret != 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_SeekFile -> fseek failed, errno {}", errno);
    return IPCReply(IPC_EINVAL);
  }

  return IPCReply(IPC_SUCCESS);
}

IPCReply Backdoor_ReadFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File handle
  // IN 1 - Pointer to a guest buffer where the data will be written to.
  // IN 2 - Amount of bytes to read
  // IO 0 - Return value (how many bytes were read)
  if (!request.HasNumberOfValidVectors(3, 1))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[0].size != 8 || request.in_vectors[1].size != 4 ||
      request.in_vectors[2].size != 4 || request.io_vectors[0].size != 4)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  u64 fp_uptr = memory.Read_U64(request.in_vectors[0].address);

  if (fp_uptr == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_ReadFile -> Invalid handle");
    return IPCReply(IPC_EINVAL);
  }

  u32 buffer_guest_ptr = memory.Read_U32(request.in_vectors[1].address);
  u32 bytes_to_read = memory.Read_U32(request.in_vectors[2].address);

  u8* host_ptr = memory.GetPointerForRange(buffer_guest_ptr, bytes_to_read);
  ASSERT(host_ptr);

  FILE* fp = reinterpret_cast<FILE*>(fp_uptr);

  u32 ret = (u32)fread(host_ptr, 1, bytes_to_read, fp);
  if (ret == 0)
  {
    if (feof(fp))
      ERROR_LOG_FMT(IOS, "Backdoor_ReadFile -> fread failed, unexpected end of file");
    else if (ferror(fp))
      ERROR_LOG_FMT(IOS, "Backdoor_ReadFile -> fread failed, errno {}", errno);
    else
      ERROR_LOG_FMT(IOS, "Backdoor_ReadFile -> fread failed, unknown error");
    return IPCReply(IPC_EINVAL);
  }

  memory.Write_U32(ret, request.io_vectors[0].address);
  return IPCReply(IPC_SUCCESS);
}

IPCReply Backdoor_WriteFile(Core::System& system, const IOCtlVRequest& request)
{
  // IN 0 - File handle
  // IN 1 - Pointer to a guest buffer where the data will be read from.
  // IN 2 - Amount of bytes to write
  // IO 0 - Return value (how many bytes were written)
  if (!request.HasNumberOfValidVectors(3, 1))
    return IPCReply(IPC_EINVAL);

  if (request.in_vectors[0].size != 8 || request.in_vectors[1].size != 4 ||
      request.in_vectors[2].size != 4 || request.io_vectors[0].size != 4)
    return IPCReply(IPC_EINVAL);

  auto& memory = system.GetMemory();
  u64 fp_uptr = memory.Read_U64(request.in_vectors[0].address);

  if (fp_uptr == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_WriteFile -> Invalid handle");
    return IPCReply(IPC_EINVAL);
  }

  u32 buffer_guest_ptr = memory.Read_U32(request.in_vectors[1].address);
  u32 bytes_to_write = memory.Read_U32(request.in_vectors[2].address);

  u8* host_ptr = memory.GetPointerForRange(buffer_guest_ptr, bytes_to_write);
  ASSERT(host_ptr);

  u32 ret = (u32)fwrite(host_ptr, 1, bytes_to_write, reinterpret_cast<FILE*>(fp_uptr));
  if (ret == 0)
  {
    ERROR_LOG_FMT(IOS, "Backdoor_WriteFile -> fwrite failed, errno {}", errno);
    return IPCReply(IPC_EINVAL);
  }

  memory.Write_U32(ret, request.io_vectors[0].address);
  return IPCReply(IPC_SUCCESS);
}

}  // namespace

IPCReply DolphinDevice::GetElapsedTime(const IOCtlVRequest& request) const
{
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  if (request.io_vectors[0].size != 4)
  {
    return IPCReply(IPC_EINVAL);
  }

  // This ioctl is used by emulated software to judge if emulation is running too fast or slow.
  // By using Common::Timer, the same clock Dolphin uses internally for the same task is exposed.
  // Return elapsed time instead of current timestamp to make buggy emulated code less likely to
  // have issues.
  const u32 milliseconds = static_cast<u32>(m_timer.ElapsedMs());

  auto& system = GetSystem();
  auto& memory = system.GetMemory();
  memory.Write_U32(milliseconds, request.io_vectors[0].address);

  return IPCReply(IPC_SUCCESS);
}

IPCReply DolphinDevice::GetSystemTime(const IOCtlVRequest& request) const
{
  if (!request.HasNumberOfValidVectors(0, 1))
  {
    return IPCReply(IPC_EINVAL);
  }

  if (request.io_vectors[0].size != 8)
  {
    return IPCReply(IPC_EINVAL);
  }

  auto& system = GetSystem();
  auto& memory = system.GetMemory();

  // Write Unix timestamp in milliseconds to memory address
  const u64 milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
  memory.Write_U64(milliseconds, request.io_vectors[0].address);
  return IPCReply(IPC_SUCCESS);
}

DolphinDevice::DolphinDevice(EmulationKernel& ios, const std::string& device_name)
    : EmulationDevice(ios, device_name)
{
  m_timer.Start();
}

std::optional<IPCReply> DolphinDevice::IOCtlV(const IOCtlVRequest& request)
{
  if (Core::WantsDeterminism())
    return IPCReply(IPC_EACCES);

  switch (request.request)
  {
  case IOCTL_DOLPHIN_GET_ELAPSED_TIME:
    return GetElapsedTime(request);
  case IOCTL_DOLPHIN_GET_VERSION:
    return GetVersion(GetSystem(), request);
  case IOCTL_DOLPHIN_GET_SPEED_LIMIT:
    return GetSpeedLimit(GetSystem(), request);
  case IOCTL_DOLPHIN_SET_SPEED_LIMIT:
    return SetSpeedLimit(GetSystem(), request);
  case IOCTL_DOLPHIN_GET_CPU_SPEED:
    return GetCPUSpeed(GetSystem(), request);
  case IOCTL_DOLPHIN_GET_REAL_PRODUCTCODE:
    return GetRealProductCode(GetSystem(), request);
  case IOCTL_DOLPHIN_DISCORD_SET_CLIENT:
    return SetDiscordClient(GetSystem(), request);
  case IOCTL_DOLPHIN_DISCORD_SET_PRESENCE:
    return SetDiscordPresence(GetSystem(), request);
  case IOCTL_DOLPHIN_DISCORD_RESET:
    return ResetDiscord(request);
  case IOCTL_DOLPHIN_GET_SYSTEM_TIME:
    return GetSystemTime(request);

  // Our extensions
  case IOCTL_DOLPHIN_ARE_EXTENSIONS_AVAILABLE:
    return AreExtensionsAvailable(request);

  case IOCTL_DOLPHIN_GET_ASPECT_RATIO:
    return GetAspectRatio(GetSystem(), request);

  case IOCTL_DOLPHIN_GET_WINDOW_RESOLUTION:
    return GetWindowResolution(GetSystem(), request);

  case IOCTL_DOLPHIN_GET_MOUSE_DATA:
    return GetMouseData(GetSystem(), request);

  // Even worse, our backdoors.
  case IOCTL_BACKDOOR_OPEN_FILE:
    return Backdoor_OpenFile(GetSystem(), request);
  case IOCTL_BACKDOOR_CLOSE_FILE:
    return Backdoor_CloseFile(GetSystem(), request);
  case IOCTL_BACKDOOR_TELL_FILE:
    return Backdoor_TellFile(GetSystem(), request);
  case IOCTL_BACKDOOR_SEEK_FILE:
    return Backdoor_SeekFile(GetSystem(), request);
  case IOCTL_BACKDOOR_READ_FILE:
    return Backdoor_ReadFile(GetSystem(), request);
  case IOCTL_BACKDOOR_WRITE_FILE:
    return Backdoor_WriteFile(GetSystem(), request);

  default:
    return IPCReply(IPC_EINVAL);
  }
}
}  // namespace IOS::HLE
