/* SPDX-License-Identifier: MPL-2.0 */

#include "ZLinkStreamConnector.h"

#include <zlink/stream_connector.hpp>

#include "runtime/connector_runtime.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace UE::ZLinkStreamConnector::Private
{

class FZLinkStreamConnectorRuntime
{
public:
  void Connect (const FString &Endpoint)
  {
    zlink::stream_connector::connector_options_t options;
    options.endpoint = Endpoint;
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;
    Connector = zlink::stream_connector::connector_factory_t::create (options);
    Connector.on_connection_state_changed (
      [this](const zlink::stream_connector::connection_state_changed_t &state) {
        LastState = Convert (state.current);
      });
    Connector.connect ();
  }

  void Close ()
  {
    Connector.close ();
    LastState = EZLinkStreamConnectionState::Closed;
  }

  void SendJson (const FName &PacketName, const FString &JsonPayload)
  {
    (void) JsonPayload;
    Connector.send (JsonPayload).packet_name (PacketName).submit ();
  }

  void RequestJson (const FName &PacketName,
                    const FString &JsonPayload,
                    float TimeoutSeconds)
  {
    (void) JsonPayload;
    Connector.request<zlink::message_t> (JsonPayload)
      .packet_name (PacketName)
      .timeout (std::chrono::milliseconds (
        static_cast<int> (TimeoutSeconds * 1000.0f)))
      .submit ();
  }

  void Dispatch ()
  {
    Connector.dispatch ();
  }

  bool IsConnected () const
  {
    return Connector.is_connected ();
  }

  int PendingDispatchCount () const
  {
    return static_cast<int> (Connector.pending_dispatch_count ());
  }

  EZLinkStreamConnectionState GetLastState () const
  {
    return LastState;
  }

private:
  static EZLinkStreamConnectionState Convert (
    zlink::stream_connector::connection_state_t state)
  {
    switch (state) {
    case zlink::stream_connector::connection_state_t::created:
      return EZLinkStreamConnectionState::Created;
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
    }
    return EZLinkStreamConnectionState::Closed;
  }

  zlink::stream_connector::connector_t Connector;
  EZLinkStreamConnectionState LastState = EZLinkStreamConnectionState::Created;
};

} // namespace UE::ZLinkStreamConnector::Private

UZLinkStreamConnector::UZLinkStreamConnector ()
  : _runtime (
      std::make_unique<
        UE::ZLinkStreamConnector::Private::FZLinkStreamConnectorRuntime> ())
{
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
UZLinkStreamConnector::RequestJson (FName PacketName,
                                    const FString &JsonPayload,
                                    float TimeoutSeconds)
{
  _runtime->RequestJson (PacketName, JsonPayload, TimeoutSeconds);
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
  return _runtime->PendingDispatchCount ();
}

EZLinkStreamConnectionState
UZLinkStreamConnector::LastState () const
{
  return _runtime->GetLastState ();
}
