// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Received } from '../messaging';
import type { Discovery } from '../service';
import type { RecvFlags } from './socket_constants';
import type { ReplyOperation, RequestOperation, SendOperation } from './socket_operations';
import type { RouterSocketOptions } from './socket_options';
import type { ConnectableSocket } from './socket';

export interface RouterSocket extends ConnectableSocket {
  readonly options: RouterSocketOptions;
  send(routingId: RoutingId): SendOperation;
  recv(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  getRoutingId(): RoutingId;
  attachDiscovery(discovery: Discovery): void;
  request(peerRid: RoutingId): RequestOperation;
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOperation;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOperation;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOperation;
}
