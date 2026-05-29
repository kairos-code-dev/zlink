// SPDX-License-Identifier: MPL-2.0

import type { MonitorSocket } from '../eventing';
import type { RoutingId } from '../core';
import type { SocketMonitorHandler } from '../service';
import type { DealerSocket } from './dealer_socket';
import type { PairSocket } from './pair_socket';
import type { PubSocket, SubSocket, XPubSocket, XSubSocket } from './pubsub_sockets';
import type { RouterSocket } from './router_socket';
import type { StreamSocket } from './stream_socket';

export interface Socket {
  bind(endpoint: string): void;
  unbind(endpoint: string): void;
  close(): void;
  monitorOpen(events?: number, handler?: SocketMonitorHandler): MonitorSocket;
  setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
}

export interface ConnectableSocket extends Socket {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  disconnectRid(routingId: RoutingId): void;
}

export type BaseSocket =
  | PairSocket
  | PubSocket
  | SubSocket
  | DealerSocket
  | RouterSocket
  | XPubSocket
  | XSubSocket
  | StreamSocket;
