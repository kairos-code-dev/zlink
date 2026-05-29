// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Received } from '../messaging';
import type { Discovery } from '../service';
import type { RecvFlags } from './socket_constants';
import type { ReplyOp, RequestOp, SendOp } from './socket_operations';
import type { RouterSocketOptions } from './socket_options';
import type { Socket } from './socket';

export interface RouterSocket extends Socket {
  readonly options: RouterSocketOptions;
  send(routingId: RoutingId): SendOp;
  recv(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  getRoutingId(): RoutingId;
  attachDiscovery(discovery: Discovery): void;
  request(peerRid: RoutingId): RequestOp;
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOp;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOp;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOp;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOp;
}
