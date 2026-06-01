/* SPDX-License-Identifier: MPL-2.0 */

#include "ZLinkStreamConnector.h"

#include <string>

int
main ()
{
  UZLinkStreamConnector connector;
  connector.Connect ("tcp://127.0.0.1:9400");
  if (!connector.IsConnected () ||
      connector.LastState () != EZLinkStreamConnectionState::Connected) {
    return 1;
  }

  connector.SendJson ("chat.send", "{\"text\":\"hello\"}");
  connector.RequestJson ("chat.request", "{\"text\":\"hello\"}", 0.01f);
  connector.Dispatch ();
  connector.Tick (0.016f);
  if (connector.PendingDispatchCount () != 0) {
    return 2;
  }

  connector.ShutdownForPie ();
  if (connector.IsConnected () ||
      connector.LastState () != EZLinkStreamConnectionState::Closed) {
    return 3;
  }

  connector.Connect ("tcp://127.0.0.1:9401");
  connector.ShutdownForMapUnload ();
  if (connector.LastState () != EZLinkStreamConnectionState::Closed) {
    return 4;
  }

  connector.Connect ("tcp://127.0.0.1:9402");
  connector.ShutdownForGameInstanceShutdown ();
  if (connector.LastState () != EZLinkStreamConnectionState::Closed) {
    return 5;
  }

  return 0;
}
