// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Received } from '../messaging';
import type { ActorBindOperation, ActorRef, ActorUnbindOperation, SpotNode, StreamPacketHandler } from '../service';
import type { RecvFlags } from './socket_constants';
import type { SendOperation } from './socket_operations';
import type { StreamSocketOptions } from './socket_options';
import type { Socket } from './socket';

export interface StreamSocket extends Socket {
  readonly options: StreamSocketOptions;
  send(routingId: RoutingId): SendOperation;
  recv(result: Received, flags?: RecvFlags): boolean;
  setPacketHandler(handler: StreamPacketHandler): void;
  setSendReadyHandler(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  getRoutingId(): RoutingId;
  disconnectRid(routingId: RoutingId): void;
  attachActorGateway(node: SpotNode): void;
  bindActor(sessionRid: RoutingId, actor: ActorRef): ActorBindOperation;
  unbindActor(sessionRid: RoutingId, actorId: string): ActorUnbindOperation;
  sendBoundActor(sessionRid: RoutingId, actorId: string): SendOperation;
  boundActors(sessionRid: RoutingId): ActorRef[];
}
