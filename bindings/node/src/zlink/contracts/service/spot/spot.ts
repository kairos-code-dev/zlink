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

/**
 * A spot: a multi-role messaging endpoint that can publish, subscribe, route,
 * request, reply, and host actors. Each send/request/reply returns a builder
 * whose parts are consumed on a successful submit.
 */
export interface Spot {
  /** The routing id that identifies this spot to its peers. */
  readonly routingId: RoutingId;
  /** Set the routing id that identifies this spot to its peers. */
  setRoutingId(routingId: RoutingId): void;
  /** Return the routing id of the node that owns this spot. */
  ownerNodeRoutingId(): RoutingId;
  /** Begin publishing under `topic`. */
  publish(topic: string): SendOperation;
  /** Receive the next matching topic message into `result`; false when `DontWait` is set and none is available. */
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  /** Add a subscription for `filter`; subscriptions accumulate. */
  setSubscription(filter: string): void;
  /** Remove a subscription previously added for `filter`. */
  unsetSubscription(filter: string): void;
  /** Return the active subscription at `index`, or null when out of range. */
  subscriptionAt(index: number): SubscriptionEntry | null;
  /** Begin a send addressed to the channel `channelName`. */
  sendToChannel(channelName: string): SendOperation;
  /** Begin a request to the channel `channelName` and await a reply. */
  requestToChannel(channelName: string): RequestOperation;
  /** Begin a send addressed to a spot on another node. */
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation;
  /** Begin a request to a spot on another node and await a reply. */
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOperation;
  /** Begin a request to a ROUTER peer `peerRid` and await a reply. */
  requestToRouter(peerRid: RoutingId): RequestOperation;
  /** Begin a reply to the spot request `requestSeq`. */
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOperation;
  /** Begin a reply to a ROUTER peer's request `requestSeq`. */
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOperation;
  /** Receive the next routed message into `result`; false when `DontWait` is set and none is available. */
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  /** Register a callback invoked when the spot can accept more sends after back-pressure (background dispatch thread). */
  setSendReadyHandler(handler: () => void): void;
  /** Register the callback invoked for each spot dispatch event (background dispatch thread). */
  setDispatchHandler(handler: SpotDispatchEventHandler): void;
  /** Receive the next pending actor-join request, or null when none is available. */
  recvActorJoin(flags?: RecvFlags): ActorJoinRequest | null;
  /** Begin a reply to `request` carrying `joinResultCode`. */
  replyActorJoin(request: ActorJoinRequest, joinResultCode: number): ActorJoinReplyOperation;
  /** Receive the next actor lifecycle event, or null when none is available. */
  recvActorLifecycle(flags?: RecvFlags): SpotActorLifecycleEvent | null;
  /** Return the actors currently hosted on this spot. */
  actors(): ActorRef[];
  /** Close the spot and release its resources. */
  close(): void;
}
