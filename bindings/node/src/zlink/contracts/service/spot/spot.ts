// SPDX-License-Identifier: MPL-2.0

import type {
  Received,
  TopicMessage,
} from '../../messaging';
import type { RoutingId } from '../../core';
import type { RecvFlags } from '../../sockets/socket_constants';
import type {
  ActorJoinReplyOperation,
  ActorJoinRequest,
  ActorRef,
  ReplyOperation,
  RequestOperation,
  SendOperation,
  SpotActorLifecycleEvent,
  SpotDispatchEventHandler,
  SubscriptionEntry,
} from '../index';

export interface Spot {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  ownerNodeRoutingId(): RoutingId;
  publish(topic: string): SendOperation;
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  setSubscription(filter: string): void;
  unsetSubscription(filter: string): void;
  subscriptionAt(index: number): SubscriptionEntry | null;
  sendToChannel(channelName: string): SendOperation;
  requestToChannel(channelName: string): RequestOperation;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOperation;
  requestToRouter(peerRid: RoutingId): RequestOperation;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOperation;
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOperation;
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
  setDispatchHandler(handler: SpotDispatchEventHandler): void;
  recvActorJoin(flags?: RecvFlags): ActorJoinRequest | null;
  replyActorJoin(request: ActorJoinRequest, joinResultCode: number): ActorJoinReplyOperation;
  recvActorLifecycle(flags?: RecvFlags): SpotActorLifecycleEvent | null;
  actors(): ActorRef[];
  close(): void;
}
