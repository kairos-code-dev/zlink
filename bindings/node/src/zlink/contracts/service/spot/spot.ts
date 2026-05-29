// SPDX-License-Identifier: MPL-2.0

import type {
  Received,
  SubscriptionEvent,
  TopicMessage,
} from '../../messaging';
import type { RoutingId } from '../../core';
import type { RecvFlags } from '../../sockets/socket_constants';
import type {
  ActorJoinReplyOp,
  ActorJoinRequest,
  ActorRef,
  ReplyOp,
  RequestOp,
  SendOp,
  SpotActorLifecycleEvent,
  SpotDispatchEventHandler,
} from '../index';

export interface Spot {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  ownerNodeRoutingId(): RoutingId;
  publish(topic: string): SendOp;
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  setSubscription(filter: string): void;
  unsetSubscription(filter: string): void;
  subscriptionAt(index: number): string;
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
  sendToChannel(channelName: string): SendOp;
  requestToChannel(channelName: string): RequestOp;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOp;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOp;
  requestToRouter(peerRid: RoutingId): RequestOp;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOp;
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOp;
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
  setDispatchHandler(handler: SpotDispatchEventHandler): void;
  recvActorJoin(flags?: RecvFlags): ActorJoinRequest | null;
  replyActorJoin(request: ActorJoinRequest, joinResultCode: number): ActorJoinReplyOp;
  recvActorLifecycle(flags?: RecvFlags): SpotActorLifecycleEvent | null;
  actors(): ActorRef[];
  close(): void;
}
