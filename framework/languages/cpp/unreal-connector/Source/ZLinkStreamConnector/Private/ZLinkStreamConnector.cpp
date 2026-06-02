/* SPDX-License-Identifier: MPL-2.0 */

#include "ZLinkStreamConnector.h"

#if __has_include("CoreMinimal.h")
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#endif

#ifdef ZLINK_UNREAL_STREAM_CONNECTOR_WITH_LZ4
#include <lz4.h>
#endif

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UE::ZLinkStreamConnector::Private
{

namespace
{

enum class frame_kind_t : std::uint8_t
{
  send = 1,
  request = 2,
  response = 3,
  error = 4,
  control = 5
};

enum class frame_codec_t : std::uint8_t
{
  raw = 0,
  json = 1,
  message_pack = 2,
  protobuf = 3
};

enum class frame_flags_t : std::uint8_t
{
  none = 0,
  has_request_seq = 0x01,
  has_metadata = 0x02,
  payload_compressed = 0x04
};

struct decoded_frame_t
{
  frame_kind_t kind = frame_kind_t::send;
  frame_codec_t codec = frame_codec_t::raw;
  frame_flags_t flags = frame_flags_t::none;
  bool compressed = false;
  std::optional<std::uint64_t> request_seq;
  std::string name;
  TMap<FString, FString> metadata;
  std::vector<std::uint8_t> payload;
};

std::string
to_utf8 (const FString &value)
{
#if __has_include("CoreMinimal.h")
  const FTCHARToUTF8 converted (*value);
  return std::string (converted.Get (),
                      converted.Get () + converted.Length ());
#else
  return value;
#endif
}

FName
to_name (const std::string &value)
{
#if __has_include("CoreMinimal.h")
  return FName (UTF8_TO_TCHAR (value.c_str ()));
#else
  return value;
#endif
}

void
append_u16 (std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
  bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
  bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void
append_u32 (std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
  bytes.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
  bytes.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
  bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
  bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void
append_u64 (std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xff));
  }
}

void
append_text (std::vector<std::uint8_t> &bytes, const std::string &value)
{
  bytes.insert (bytes.end (), value.begin (), value.end ());
}

std::uint16_t
read_u16 (const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
  return static_cast<std::uint16_t> (
    (static_cast<std::uint16_t> (bytes[offset]) << 8) |
    static_cast<std::uint16_t> (bytes[offset + 1]));
}

std::uint32_t
read_u32 (const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
  return (static_cast<std::uint32_t> (bytes[offset]) << 24) |
         (static_cast<std::uint32_t> (bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t> (bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t> (bytes[offset + 3]);
}

std::uint64_t
read_u64 (const std::vector<std::uint8_t> &bytes, std::size_t &offset)
{
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | bytes[offset + static_cast<std::size_t> (i)];
  }
  offset += 8;
  return value;
}

std::string
read_text (const std::vector<std::uint8_t> &bytes,
           std::size_t &offset,
           std::size_t size)
{
  std::string value (
    bytes.begin () + static_cast<std::ptrdiff_t> (offset),
    bytes.begin () + static_cast<std::ptrdiff_t> (offset + size));
  offset += size;
  return value;
}

bool
has_flag (std::uint8_t flags, frame_flags_t flag)
{
  return (flags & static_cast<std::uint8_t> (flag)) != 0;
}

void
append_lz4_length (std::vector<std::uint8_t> &bytes, std::size_t value)
{
  while (value >= 255) {
    bytes.push_back (255);
    value -= 255;
  }
  bytes.push_back (static_cast<std::uint8_t> (value));
}

std::vector<std::uint8_t>
encode_lz4_payload (const std::vector<std::uint8_t> &payload)
{
  std::vector<std::uint8_t> output;
  append_u32 (output, static_cast<std::uint32_t> (payload.size ()));
  if (payload.empty ()) {
    return output;
  }
#ifdef ZLINK_UNREAL_STREAM_CONNECTOR_WITH_LZ4
  if (payload.size () > static_cast<std::size_t> (
                         std::numeric_limits<int>::max ())) {
    return output;
  }
  const int bound = LZ4_compressBound (static_cast<int> (payload.size ()));
  std::vector<std::uint8_t> compressed (static_cast<std::size_t> (bound));
  const int written = LZ4_compress_default (
    reinterpret_cast<const char *> (payload.data ()),
    reinterpret_cast<char *> (compressed.data ()),
    static_cast<int> (payload.size ()),
    bound);
  if (written > 0) {
    output.insert (output.end (),
                   compressed.begin (),
                   compressed.begin () + written);
    return output;
  }
#endif

  const auto literal_token =
    static_cast<std::uint8_t> (std::min<std::size_t> (payload.size (), 15)
                               << 4);
  output.push_back (literal_token);
  if (payload.size () >= 15) {
    append_lz4_length (output, payload.size () - 15);
  }
  output.insert (output.end (), payload.begin (), payload.end ());
  return output;
}

std::optional<std::vector<std::uint8_t>>
decode_lz4_payload (const std::vector<std::uint8_t> &payload)
{
  if (payload.size () < 4) {
    return std::nullopt;
  }
  const auto original_size = read_u32 (payload, 0);
  std::vector<std::uint8_t> output;
  output.reserve (original_size);
  if (original_size == 0) {
    return output;
  }
#ifdef ZLINK_UNREAL_STREAM_CONNECTOR_WITH_LZ4
  if (original_size > static_cast<std::uint32_t> (
                        std::numeric_limits<int>::max ())) {
    return std::nullopt;
  }
  output.resize (original_size);
  const int decoded = LZ4_decompress_safe (
    reinterpret_cast<const char *> (payload.data () + 4),
    reinterpret_cast<char *> (output.data ()),
    static_cast<int> (payload.size () - 4),
    static_cast<int> (output.size ()));
  if (decoded == static_cast<int> (output.size ())) {
    return output;
  }
  output.clear ();
#endif

  std::size_t offset = 4;
  while (offset < payload.size ()) {
    const auto token = payload[offset++];
    std::size_t literal_size = token >> 4;
    if (literal_size == 15) {
      while (offset < payload.size () && payload[offset] == 255) {
        literal_size += 255;
        ++offset;
      }
      if (offset >= payload.size ()) {
        return std::nullopt;
      }
      literal_size += payload[offset++];
    }
    if (payload.size () - offset < literal_size) {
      return std::nullopt;
    }
    output.insert (output.end (),
                   payload.begin () + static_cast<std::ptrdiff_t> (offset),
                   payload.begin () +
                     static_cast<std::ptrdiff_t> (offset + literal_size));
    offset += literal_size;
    if (offset == payload.size ()) {
      break;
    }
    if (payload.size () - offset < 2) {
      return std::nullopt;
    }
    const std::size_t match_offset =
      static_cast<std::size_t> (payload[offset]) |
      (static_cast<std::size_t> (payload[offset + 1]) << 8);
    offset += 2;
    if (match_offset == 0 || match_offset > output.size ()) {
      return std::nullopt;
    }

    std::size_t match_size = (token & 0x0f) + 4;
    if ((token & 0x0f) == 15) {
      while (offset < payload.size () && payload[offset] == 255) {
        match_size += 255;
        ++offset;
      }
      if (offset >= payload.size ()) {
        return std::nullopt;
      }
      match_size += payload[offset++];
    }
    for (std::size_t index = 0; index < match_size; ++index) {
      output.push_back (output[output.size () - match_offset]);
    }
    if (output.size () > original_size) {
      return std::nullopt;
    }
  }
  if (output.size () != original_size) {
    return std::nullopt;
  }
  return output;
}

void
add_metadata (TMap<FString, FString> &metadata,
              const std::string &key,
              const std::string &value)
{
#if __has_include("CoreMinimal.h")
  metadata.Add (FString (UTF8_TO_TCHAR (key.c_str ())),
                FString (UTF8_TO_TCHAR (value.c_str ())));
#else
  metadata.emplace (key, value);
#endif
}

bool
metadata_empty (const TMap<FString, FString> &metadata)
{
#if __has_include("CoreMinimal.h")
  return metadata.IsEmpty ();
#else
  return metadata.empty ();
#endif
}

std::size_t
metadata_count (const TMap<FString, FString> &metadata)
{
#if __has_include("CoreMinimal.h")
  return static_cast<std::size_t> (metadata.Num ());
#else
  return metadata.size ();
#endif
}

std::vector<std::uint8_t>
encode_metadata (const TMap<FString, FString> &metadata)
{
  std::vector<std::uint8_t> bytes;
  if (metadata_empty (metadata)) {
    return bytes;
  }
  bytes.push_back (static_cast<std::uint8_t> (metadata_count (metadata)));
#if __has_include("CoreMinimal.h")
  for (const auto &entry : metadata) {
    const auto key = to_utf8 (entry.Key);
    const auto value = to_utf8 (entry.Value);
    bytes.push_back (static_cast<std::uint8_t> (key.size ()));
    append_text (bytes, key);
    append_u16 (bytes, static_cast<std::uint16_t> (value.size ()));
    append_text (bytes, value);
  }
#else
  for (const auto &[key_text, value_text] : metadata) {
    const auto key = to_utf8 (key_text);
    const auto value = to_utf8 (value_text);
    bytes.push_back (static_cast<std::uint8_t> (key.size ()));
    append_text (bytes, key);
    append_u16 (bytes, static_cast<std::uint16_t> (value.size ()));
    append_text (bytes, value);
  }
#endif
  return bytes;
}

TMap<FString, FString>
decode_metadata (const std::vector<std::uint8_t> &bytes)
{
  TMap<FString, FString> metadata;
  if (bytes.empty ()) {
    return metadata;
  }
  std::size_t offset = 0;
  const auto count = bytes[offset++];
  for (std::uint8_t index = 0; index < count; ++index) {
    const auto key_size = bytes[offset++];
    const auto key = read_text (bytes, offset, key_size);
    const auto value_size = read_u16 (bytes, offset);
    offset += 2;
    const auto value = read_text (bytes, offset, value_size);
    add_metadata (metadata, key, value);
  }
  return metadata;
}

std::vector<std::uint8_t>
encode_frame (frame_kind_t kind,
              std::optional<std::uint64_t> request_seq,
              const std::string &name,
              const std::string &payload,
              const TMap<FString, FString> &metadata,
              bool compressed)
{
  auto metadata_bytes = encode_metadata (metadata);
  std::vector<std::uint8_t> payload_bytes (payload.begin (), payload.end ());
  std::vector<std::uint8_t> header;
  header.push_back (static_cast<std::uint8_t> (kind));
  header.push_back (static_cast<std::uint8_t> (frame_codec_t::json));
  std::uint8_t flags_value = 0;
  if (request_seq) {
    flags_value |= static_cast<std::uint8_t> (
      frame_flags_t::has_request_seq);
  }
  if (!metadata_bytes.empty ()) {
    flags_value |= static_cast<std::uint8_t> (frame_flags_t::has_metadata);
  }
  if (compressed) {
    flags_value |= static_cast<std::uint8_t> (
      frame_flags_t::payload_compressed);
    payload_bytes = encode_lz4_payload (payload_bytes);
  }
  header.push_back (flags_value);
  if (request_seq) {
    append_u64 (header, *request_seq);
  }
  header.push_back (static_cast<std::uint8_t> (name.size ()));
  header.insert (header.end (), name.begin (), name.end ());
  if (!metadata_bytes.empty ()) {
    append_u16 (header, static_cast<std::uint16_t> (metadata_bytes.size ()));
    header.insert (header.end (), metadata_bytes.begin (),
                   metadata_bytes.end ());
  }

  std::vector<std::uint8_t> frame;
  frame.reserve (6 + header.size () + payload_bytes.size ());
  append_u16 (frame, static_cast<std::uint16_t> (header.size ()));
  append_u32 (frame, static_cast<std::uint32_t> (payload_bytes.size ()));
  frame.insert (frame.end (), header.begin (), header.end ());
  frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
  return frame;
}

std::optional<decoded_frame_t>
try_decode_frame (std::vector<std::uint8_t> &buffer)
{
  if (buffer.size () < 6) {
    return std::nullopt;
  }
  const auto header_size = read_u16 (buffer, 0);
  const auto payload_size = read_u32 (buffer, 2);
  const auto frame_size =
    6 + static_cast<std::size_t> (header_size) + payload_size;
  if (buffer.size () < frame_size) {
    return std::nullopt;
  }

  std::size_t offset = 6;
  decoded_frame_t decoded;
  decoded.kind = static_cast<frame_kind_t> (buffer[offset++]);
  decoded.codec = static_cast<frame_codec_t> (buffer[offset++]);
  decoded.flags = static_cast<frame_flags_t> (buffer[offset++]);
  const auto flags_value = static_cast<std::uint8_t> (decoded.flags);
  if (has_flag (flags_value, frame_flags_t::has_request_seq)) {
    decoded.request_seq = read_u64 (buffer, offset);
  }
  const auto name_size = buffer[offset++];
  decoded.name = read_text (buffer, offset, name_size);
  if (has_flag (flags_value, frame_flags_t::has_metadata)) {
    const auto metadata_size = read_u16 (buffer, offset);
    offset += 2;
    std::vector<std::uint8_t> metadata_bytes (
      buffer.begin () + static_cast<std::ptrdiff_t> (offset),
      buffer.begin () + static_cast<std::ptrdiff_t> (offset + metadata_size));
    offset += metadata_size;
    decoded.metadata = decode_metadata (metadata_bytes);
  }
  const auto payload_offset = 6 + static_cast<std::size_t> (header_size);
  decoded.payload.assign (
    buffer.begin () + static_cast<std::ptrdiff_t> (payload_offset),
    buffer.begin () + static_cast<std::ptrdiff_t> (frame_size));
  buffer.erase (buffer.begin (),
                buffer.begin () + static_cast<std::ptrdiff_t> (frame_size));
  decoded.compressed = has_flag (flags_value, frame_flags_t::payload_compressed);
  if (decoded.compressed) {
    auto decompressed = decode_lz4_payload (decoded.payload);
    if (!decompressed) {
      return std::nullopt;
    }
    decoded.payload = std::move (*decompressed);
  }
  return decoded;
}

} // namespace

class FZLinkStreamConnectorRuntime
{
public:
  void SetStateChangedCallback (
    std::function<void (EZLinkStreamConnectionState)> callback)
  {
    StateChanged = std::move (callback);
  }

  void SetPacketReceivedCallback (
    std::function<void (FZLinkStreamPacket)> callback)
  {
    PacketReceived = std::move (callback);
  }

  void SetRequestCompletedCallback (
    std::function<void (FZLinkStreamPacket)> callback)
  {
    RequestCompleted = std::move (callback);
  }

  void Connect (const FString &Endpoint)
  {
    EndpointName = Endpoint;
    LastState = EZLinkStreamConnectionState::Connecting;
    BroadcastStateChanged (LastState);
#if __has_include("CoreMinimal.h")
    CloseSocketOnly ();
    FString Host;
    int32 Port = 0;
    if (!ParseTcpEndpoint (Endpoint, Host, Port)) {
      LastState = EZLinkStreamConnectionState::Disconnected;
      BroadcastStateChanged (LastState);
      return;
    }
    ISocketSubsystem *SocketSubsystem =
      ISocketSubsystem::Get (PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem) {
      LastState = EZLinkStreamConnectionState::Disconnected;
      BroadcastStateChanged (LastState);
      return;
    }
    bool bValidAddress = false;
    TSharedRef<FInternetAddr> Address =
      SocketSubsystem->CreateInternetAddr ();
    Address->SetIp (*Host, bValidAddress);
    Address->SetPort (Port);
    if (!bValidAddress) {
      LastState = EZLinkStreamConnectionState::Disconnected;
      BroadcastStateChanged (LastState);
      return;
    }
    Socket.Reset (
      SocketSubsystem->CreateSocket (NAME_Stream, TEXT ("ZLinkStreamConnector"),
                                     false));
    if (!Socket || !Socket->Connect (*Address)) {
      CloseSocketOnly ();
      LastState = EZLinkStreamConnectionState::Disconnected;
      BroadcastStateChanged (LastState);
      return;
    }
#endif
    LastState = EZLinkStreamConnectionState::Connected;
    BroadcastStateChanged (LastState);
  }

  void Close ()
  {
#if __has_include("CoreMinimal.h")
    CloseSocketOnly ();
    PendingRequests.clear ();
#endif
    LastState = EZLinkStreamConnectionState::Closed;
    BroadcastStateChanged (LastState);
  }

  void SendJson (const FName &PacketName, const FString &JsonPayload)
  {
    SendJson (PacketName, JsonPayload, FZLinkStreamSendOptions {});
  }

  void SendJson (const FName &PacketName,
                 const FString &JsonPayload,
                 const FZLinkStreamSendOptions &Options)
  {
    LastPacketName = PacketName;
    LastJsonPayload = JsonPayload;
#if __has_include("CoreMinimal.h")
    SendFrame (encode_frame (frame_kind_t::send,
                             std::nullopt,
                             to_utf8 (PacketName.ToString ()),
                             to_utf8 (JsonPayload),
                             Options.Metadata,
                             Options.bCompress));
#endif
  }

  void RequestJson (const FName &PacketName,
                    const FString &JsonPayload,
                    float TimeoutSeconds)
  {
    RequestJson (PacketName,
                 JsonPayload,
                 TimeoutSeconds,
                 FZLinkStreamSendOptions {});
  }

  void RequestJson (const FName &PacketName,
                    const FString &JsonPayload,
                    float TimeoutSeconds,
                    const FZLinkStreamSendOptions &Options)
  {
    (void) TimeoutSeconds;
    LastPacketName = PacketName;
    LastJsonPayload = JsonPayload;
#if __has_include("CoreMinimal.h")
    ++NextRequestSeq;
    PendingRequests.emplace (NextRequestSeq, to_utf8 (PacketName.ToString ()));
    SendFrame (encode_frame (frame_kind_t::request,
                             NextRequestSeq,
                             to_utf8 (PacketName.ToString ()),
                             to_utf8 (JsonPayload),
                             Options.Metadata,
                             Options.bCompress));
#endif
  }

  void Dispatch ()
  {
#if __has_include("CoreMinimal.h")
    PumpIncomingFrames ();
    while (!DispatchQueue.empty ()) {
      auto frame = std::move (DispatchQueue.front ());
      DispatchQueue.erase (DispatchQueue.begin ());
      DispatchFrame (std::move (frame));
    }
#endif
  }

  bool IsConnected () const
  {
    return LastState == EZLinkStreamConnectionState::Connected;
  }

  int PendingDispatchCount ()
  {
#if __has_include("CoreMinimal.h")
    PumpIncomingFrames ();
    return static_cast<int> (DispatchQueue.size ());
#else
    return 0;
#endif
  }

  EZLinkStreamConnectionState GetLastState () const
  {
    return LastState;
  }

private:
  void BroadcastStateChanged (EZLinkStreamConnectionState state)
  {
    if (StateChanged) {
      StateChanged (state);
    }
  }

#if __has_include("CoreMinimal.h")
  static bool ParseTcpEndpoint (const FString &Endpoint,
                                FString &Host,
                                int32 &Port)
  {
    constexpr const TCHAR *Prefix = TEXT ("tcp://");
    if (!Endpoint.StartsWith (Prefix)) {
      return false;
    }
    FString Remainder = Endpoint.RightChop (FCString::Strlen (Prefix));
    FString PortText;
    if (!Remainder.Split (TEXT (":"), &Host, &PortText,
                          ESearchCase::CaseSensitive,
                          ESearchDir::FromEnd)) {
      return false;
    }
    Port = FCString::Atoi (*PortText);
    return !Host.IsEmpty () && Port > 0;
  }

  void SendFrame (const std::vector<std::uint8_t> &frame)
  {
    if (!Socket) {
      return;
    }
    int32 BytesSent = 0;
    Socket->Send (frame.data (), static_cast<int32> (frame.size ()),
                  BytesSent);
  }

  void CloseSocketOnly ()
  {
    if (!Socket) {
      return;
    }
    Socket->Close ();
    ISocketSubsystem *SocketSubsystem =
      ISocketSubsystem::Get (PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem) {
      SocketSubsystem->DestroySocket (Socket.Release ());
    }
  }

  void PumpIncomingFrames ()
  {
    if (!Socket) {
      return;
    }
    uint32 PendingDataSize = 0;
    while (Socket->HasPendingData (PendingDataSize) && PendingDataSize > 0) {
      TArray<uint8> Chunk;
      Chunk.SetNumUninitialized (PendingDataSize);
      int32 BytesRead = 0;
      if (!Socket->Recv (Chunk.GetData (), Chunk.Num (), BytesRead) ||
          BytesRead <= 0) {
        break;
      }
      ReceiveBuffer.insert (ReceiveBuffer.end (),
                            Chunk.GetData (),
                            Chunk.GetData () + BytesRead);
    }
    while (auto frame = try_decode_frame (ReceiveBuffer)) {
      DispatchQueue.push_back (std::move (*frame));
    }
  }

  void DispatchFrame (decoded_frame_t frame)
  {
    if (frame.kind == frame_kind_t::send) {
      FZLinkStreamPacket Packet = MakePacket (std::move (frame));
      if (PacketReceived) {
        PacketReceived (Packet);
      }
    } else if (frame.kind == frame_kind_t::response &&
               frame.request_seq &&
               PendingRequests.erase (*frame.request_seq) > 0) {
      FZLinkStreamPacket Packet = MakePacket (std::move (frame));
      if (RequestCompleted) {
        RequestCompleted (Packet);
      }
    }
  }

  static FZLinkStreamPacket MakePacket (decoded_frame_t frame)
  {
    FZLinkStreamPacket Packet;
    Packet.PacketName = to_name (frame.name);
    Packet.Metadata = std::move (frame.metadata);
    Packet.bCompressed = frame.compressed;
    Packet.Payload.Append (
      frame.payload.data (),
      static_cast<int32> (frame.payload.size ()));
    return Packet;
  }
#endif

  FString EndpointName;
  FName LastPacketName;
  FString LastJsonPayload;
  EZLinkStreamConnectionState LastState = EZLinkStreamConnectionState::Created;
  std::function<void (EZLinkStreamConnectionState)> StateChanged;
  std::function<void (FZLinkStreamPacket)> PacketReceived;
  std::function<void (FZLinkStreamPacket)> RequestCompleted;
#if __has_include("CoreMinimal.h")
  TUniquePtr<FSocket> Socket;
  std::vector<std::uint8_t> ReceiveBuffer;
  std::vector<decoded_frame_t> DispatchQueue;
  std::uint64_t NextRequestSeq = 0;
  std::map<std::uint64_t, std::string> PendingRequests;
#endif
};

} // namespace UE::ZLinkStreamConnector::Private

UZLinkStreamConnector::UZLinkStreamConnector ()
  : _runtime (
      std::make_unique<
        UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime> ())
{
  _runtime->SetStateChangedCallback (
    [this](EZLinkStreamConnectionState state) {
      OnConnectionStateChanged.Broadcast (state);
    });
  _runtime->SetPacketReceivedCallback (
    [this](FZLinkStreamPacket packet) {
      OnPacketReceived.Broadcast (packet);
      OnPacketReceivedNative.Broadcast (packet);
    });
  _runtime->SetRequestCompletedCallback (
    [this](FZLinkStreamPacket packet) {
      OnRequestCompleted.Broadcast (packet);
      OnRequestCompletedNative.Broadcast (packet);
    });
}

UZLinkStreamConnector::~UZLinkStreamConnector () = default;

void
UZLinkStreamConnector::Connect (const FString &Endpoint)
{
  _runtime->Connect (Endpoint);
}

void
UZLinkStreamConnector::Close ()
{
  _runtime->Close ();
}

void
UZLinkStreamConnector::SendJson (FName PacketName, const FString &JsonPayload)
{
  _runtime->SendJson (PacketName, JsonPayload);
}

void
UZLinkStreamConnector::SendJsonWithOptions (
  FName PacketName,
  const FString &JsonPayload,
  const FZLinkStreamSendOptions &Options)
{
  _runtime->SendJson (PacketName, JsonPayload, Options);
}

void
UZLinkStreamConnector::RequestJson (FName PacketName,
                                    const FString &JsonPayload,
                                    float TimeoutSeconds)
{
  _runtime->RequestJson (PacketName, JsonPayload, TimeoutSeconds);
}

void
UZLinkStreamConnector::RequestJsonWithOptions (
  FName PacketName,
  const FString &JsonPayload,
  float TimeoutSeconds,
  const FZLinkStreamSendOptions &Options)
{
  _runtime->RequestJson (PacketName, JsonPayload, TimeoutSeconds, Options);
}

void
UZLinkStreamConnector::Dispatch ()
{
  _runtime->Dispatch ();
}

void
UZLinkStreamConnector::Tick (float DeltaSeconds)
{
  (void) DeltaSeconds;
  _runtime->Dispatch ();
}

void
UZLinkStreamConnector::ShutdownForPie ()
{
  _runtime->Close ();
}

void
UZLinkStreamConnector::ShutdownForMapUnload ()
{
  _runtime->Close ();
}

void
UZLinkStreamConnector::ShutdownForGameInstanceShutdown ()
{
  _runtime->Close ();
}

bool
UZLinkStreamConnector::IsConnected () const
{
  return _runtime->IsConnected ();
}

int
UZLinkStreamConnector::PendingDispatchCount () const
{
  return const_cast<
           UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime *> (
           _runtime.get ())
    ->PendingDispatchCount ();
}

EZLinkStreamConnectionState
UZLinkStreamConnector::LastState () const
{
  return _runtime->GetLastState ();
}
