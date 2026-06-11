/* SPDX-License-Identifier: MPL-2.0 */

#include "ZLinkStreamConnector.h"

#include <zlink/stream_connector.hpp>

#if __has_include("CoreMinimal.h")
#include "UObject/WeakObjectPtr.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UE::ZLinkStreamConnector::Private
{

namespace
{

std::string to_utf8 (const FString &value)
{
#if __has_include("CoreMinimal.h")
    const FTCHARToUTF8 converted (*value);
    return std::string (converted.Get (), converted.Get () + converted.Length ());
#else
    return value;
#endif
}

zlink::stream_connector::metadata_t to_metadata (const TMap<FString, FString> &metadata)
{
    zlink::stream_connector::metadata_t converted;
#if __has_include("CoreMinimal.h")
    for (const auto &entry : metadata) {
        converted.with (to_utf8 (entry.Key), to_utf8 (entry.Value));
    }
#else
    for (const auto &entry : metadata) {
        converted.with (entry.first, entry.second);
    }
#endif
    return converted;
}

zlink::message_t to_payload (const FString &json)
{
    return zlink::message_t::from (to_utf8 (json));
}

EZLinkStreamConnectionState to_unreal_state (zlink::stream_connector::connection_state_t state)
{
    switch (state) {
    case zlink::stream_connector::connection_state_t::connecting:
        return EZLinkStreamConnectionState::Connecting;
    case zlink::stream_connector::connection_state_t::connected:
        return EZLinkStreamConnectionState::Connected;
    case zlink::stream_connector::connection_state_t::reconnecting:
        return EZLinkStreamConnectionState::Reconnecting;
    case zlink::stream_connector::connection_state_t::disconnected:
        return EZLinkStreamConnectionState::Disconnected;
    case zlink::stream_connector::connection_state_t::closed:
        return EZLinkStreamConnectionState::Closed;
    case zlink::stream_connector::connection_state_t::created:
    default:
        return EZLinkStreamConnectionState::Created;
    }
}

} // namespace

class FZLinkStreamConnectorRuntime
{
  public:
    explicit FZLinkStreamConnectorRuntime (UZLinkStreamConnector &Owner) :
#if __has_include("CoreMinimal.h")
        _owner (&Owner)
#else
        _owner (&Owner)
#endif
    {
    }

    void DetachOwner ()
    {
#if __has_include("CoreMinimal.h")
        _owner.Reset ();
#else
        _owner = nullptr;
#endif
        CancelCallbacks = true;
        PendingPackets.clear ();
        PendingRequests.clear ();
        PendingStates.clear ();
    }

    void Connect (const FString &Endpoint)
    {
        CancelCallbacks = false;
        zlink::stream_connector::connector_options_t options;
        options.endpoint = to_utf8 (Endpoint);
        options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;
        Connector = zlink::stream_connector::connector_factory_t::create (std::move (options));
        Connector.on_connection_state_changed (
          [this] (const zlink::stream_connector::connection_state_changed_t &event) {
              EnqueueState (to_unreal_state (event.current));
          });

        LastConnectionState = EZLinkStreamConnectionState::Connecting;
        EnqueueState (EZLinkStreamConnectionState::Connecting);

        const auto connected = Connector.connect ();
        if (connected) {
            LastConnectionState = EZLinkStreamConnectionState::Connected;
            EnqueueState (EZLinkStreamConnectionState::Connected);
            return;
        }

        LastConnectionState = EZLinkStreamConnectionState::Disconnected;
        EnqueueState (EZLinkStreamConnectionState::Disconnected);
    }

    void Close ()
    {
        CancelCallbacks = true;
        Connector.close ();
        PendingPackets.clear ();
        PendingRequests.clear ();
        PendingStates.clear ();
        LastConnectionState = EZLinkStreamConnectionState::Closed;
        EnqueueState (EZLinkStreamConnectionState::Closed);
    }

    void SendJson (const FName &PacketName, const FString &JsonPayload)
    {
        SendJson (PacketName, JsonPayload, FZLinkStreamSendOptions{});
    }

    void SendJson (const FName &PacketName,
                   const FString &JsonPayload,
                   const FZLinkStreamSendOptions &Options)
    {
        auto call = Connector.send (MakePacket (PacketName, JsonPayload, Options));
        if (Options.bCompress) {
            call.compress ();
        }
        call.submit ([] (zlink::stream_connector::result_t<void>) {});
    }

    void RequestJson (const FName &PacketName, const FString &JsonPayload, float TimeoutSeconds)
    {
        RequestJson (PacketName, JsonPayload, TimeoutSeconds, FZLinkStreamSendOptions{});
    }

    void RequestJson (const FName &PacketName,
                      const FString &JsonPayload,
                      float TimeoutSeconds,
                      const FZLinkStreamSendOptions &Options)
    {
        auto request = Connector.request<zlink::stream_connector::packet_t> (
          MakePacket (PacketName, JsonPayload, Options));
        request.timeout (
          std::chrono::milliseconds (std::max (1, static_cast<int> (TimeoutSeconds * 1000.0f))));
        if (Options.bCompress) {
            request.compress ();
        }
        request.submit ([this] (zlink::stream_connector::result_t<zlink::stream_connector::packet_t>
                                  result) {
            if (!result || CancelCallbacks) {
                return;
            }
            PendingRequests.push_back (ToUnrealPacket (result.value ()));
        });
    }

    void Dispatch ()
    {
        Connector.dispatch ();
        UZLinkStreamConnector *owner = Owner ();
        if (!owner) {
            PendingPackets.clear ();
            PendingRequests.clear ();
            PendingStates.clear ();
            return;
        }

        while (!PendingStates.empty ()) {
            const auto state = PendingStates.front ();
            PendingStates.erase (PendingStates.begin ());
            LastConnectionState = state;
            owner->OnConnectionStateChanged.Broadcast (state);
        }
        if (CancelCallbacks) {
            PendingPackets.clear ();
            PendingRequests.clear ();
            return;
        }
        while (!PendingPackets.empty ()) {
            auto packet = std::move (PendingPackets.front ());
            PendingPackets.erase (PendingPackets.begin ());
            owner->OnPacketReceived.Broadcast (packet);
            owner->OnPacketReceivedNative.Broadcast (packet);
        }
        while (!PendingRequests.empty ()) {
            auto packet = std::move (PendingRequests.front ());
            PendingRequests.erase (PendingRequests.begin ());
            owner->OnRequestCompleted.Broadcast (packet);
            owner->OnRequestCompletedNative.Broadcast (packet);
        }
    }

    bool IsConnected () const { return Connector.is_connected (); }

    int PendingDispatchCount () const
    {
        return static_cast<int> (PendingStates.size () + PendingPackets.size ()
                                 + PendingRequests.size ()
                                 + Connector.pending_dispatch_count ());
    }

    EZLinkStreamConnectionState LastState () const { return LastConnectionState; }

  private:
    zlink::stream_connector::packet_t MakePacket (const FName &PacketName,
                                                  const FString &JsonPayload,
                                                  const FZLinkStreamSendOptions &Options)
    {
        zlink::stream_connector::packet_t packet;
#if __has_include("CoreMinimal.h")
        packet.name = to_utf8 (PacketName.ToString ());
#else
        packet.name = PacketName;
#endif
        packet.codec = zlink::stream_connector::codec_t::json;
        packet.metadata = to_metadata (Options.Metadata);
        packet.payload = to_payload (JsonPayload);
        return packet;
    }

    FZLinkStreamPacket ToUnrealPacket (const zlink::stream_connector::packet_t &packet)
    {
        FZLinkStreamPacket converted;
#if __has_include("CoreMinimal.h")
        converted.PacketName = FName (UTF8_TO_TCHAR (packet.name.c_str ()));
#else
        converted.PacketName = packet.name;
#endif
        const auto payload = packet.payload.to_string ();
        converted.Payload.assign (payload.begin (), payload.end ());
#if __has_include("CoreMinimal.h")
        for (const auto &entry : packet.metadata.values) {
            converted.Metadata.Add (FString (UTF8_TO_TCHAR (entry.first.c_str ())),
                                    FString (UTF8_TO_TCHAR (entry.second.c_str ())));
        }
#else
        for (const auto &entry : packet.metadata.values) {
            converted.Metadata.emplace (entry.first, entry.second);
        }
#endif
        converted.bCompressed = false;
        return converted;
    }

    void EnqueueState (EZLinkStreamConnectionState State)
    {
        PendingStates.push_back (State);
    }

    UZLinkStreamConnector *Owner () const
    {
#if __has_include("CoreMinimal.h")
        return _owner.Get ();
#else
        return _owner;
#endif
    }

#if __has_include("CoreMinimal.h")
    TWeakObjectPtr<UZLinkStreamConnector> _owner;
#else
    UZLinkStreamConnector *_owner;
#endif
    zlink::stream_connector::connector_t Connector;
    bool CancelCallbacks = false;
    EZLinkStreamConnectionState LastConnectionState = EZLinkStreamConnectionState::Created;
    std::vector<EZLinkStreamConnectionState> PendingStates;
    std::vector<FZLinkStreamPacket> PendingPackets;
    std::vector<FZLinkStreamPacket> PendingRequests;
};

} // namespace UE::ZLinkStreamConnector::Private

UZLinkStreamConnector::UZLinkStreamConnector () :
    _runtime (std::make_unique<UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime> (*this))
{
}

UZLinkStreamConnector::~UZLinkStreamConnector ()
{
    if (_runtime) {
        _runtime->DetachOwner ();
    }
}

void UZLinkStreamConnector::Connect (const FString &Endpoint)
{
    _runtime->Connect (Endpoint);
}

void UZLinkStreamConnector::Close ()
{
    _runtime->Close ();
}

void UZLinkStreamConnector::SendJson (FName PacketName, const FString &JsonPayload)
{
    _runtime->SendJson (PacketName, JsonPayload);
}

void UZLinkStreamConnector::SendJsonWithOptions (FName PacketName,
                                                 const FString &JsonPayload,
                                                 const FZLinkStreamSendOptions &Options)
{
    _runtime->SendJson (PacketName, JsonPayload, Options);
}

void UZLinkStreamConnector::RequestJson (FName PacketName,
                                         const FString &JsonPayload,
                                         float TimeoutSeconds)
{
    _runtime->RequestJson (PacketName, JsonPayload, TimeoutSeconds);
}

void UZLinkStreamConnector::RequestJsonWithOptions (FName PacketName,
                                                    const FString &JsonPayload,
                                                    float TimeoutSeconds,
                                                    const FZLinkStreamSendOptions &Options)
{
    _runtime->RequestJson (PacketName, JsonPayload, TimeoutSeconds, Options);
}

void UZLinkStreamConnector::Dispatch ()
{
    _runtime->Dispatch ();
}

void UZLinkStreamConnector::Tick (float)
{
    _runtime->Dispatch ();
}

void UZLinkStreamConnector::ShutdownForPie ()
{
    _runtime->Close ();
}

void UZLinkStreamConnector::ShutdownForMapUnload ()
{
    _runtime->Close ();
}

void UZLinkStreamConnector::ShutdownForGameInstanceShutdown ()
{
    _runtime->Close ();
}

bool UZLinkStreamConnector::IsConnected () const
{
    return const_cast<UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime *> (
             _runtime.get ())
      ->IsConnected ();
}

int UZLinkStreamConnector::PendingDispatchCount () const
{
    return const_cast<UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime *> (
             _runtime.get ())
      ->PendingDispatchCount ();
}

EZLinkStreamConnectionState UZLinkStreamConnector::LastState () const
{
    return const_cast<UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime *> (
             _runtime.get ())
      ->LastState ();
}
