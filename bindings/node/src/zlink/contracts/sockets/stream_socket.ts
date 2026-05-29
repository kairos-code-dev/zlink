// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Received } from '../messaging';
import type { ActorBindOp, ActorRef, ActorUnbindOp, SpotNode, StreamPacketHandler } from '../service';
import type { RecvFlags } from './socket_constants';
import type { SendOp } from './socket_operations';
import type { StreamSocketOptions } from './socket_options';
import type { Socket } from './socket';

export interface StreamSocket extends Socket {
  readonly options: StreamSocketOptions;
  send(routingId: RoutingId): SendOp;
  recv(result: Received, flags?: RecvFlags): boolean;
  setPacketHandler(handler: StreamPacketHandler): void;
  setSendReadyHandler(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  getRoutingId(): RoutingId;
  attachActorGateway(node: SpotNode): void;
  bindActor(sessionRid: RoutingId, actor: ActorRef): ActorBindOp;
  unbindActor(sessionRid: RoutingId, actorId: string): ActorUnbindOp;
  sendBoundActor(sessionRid: RoutingId, actorId: string): SendOp;
  boundActors(sessionRid: RoutingId): ActorRef[];
}
