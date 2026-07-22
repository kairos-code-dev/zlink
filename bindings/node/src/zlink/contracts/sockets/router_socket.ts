// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Received } from '../messaging';
import type { ReplyOperation, RequestOperation, SendOperation } from '../messaging';
import type { RecvFlags } from './socket_constants';
import type { RouterSocketOptions } from './socket_options';
import type { ConnectableSocket } from './socket';

/**
 * ROUTER socket: routes messages to peers addressed by routing id, the server
 * side of asynchronous request/reply.
 */
export interface RouterSocket extends ConnectableSocket {
  /** The ROUTER-specific typed options facade. */
  readonly options: RouterSocketOptions;
  /** Begin a send addressed to `routingId`; parts are consumed on a successful submit. */
  send(routingId: RoutingId): SendOperation;
  /**
   * Receive a routed message into `result`; false when `RecvFlags.DontWait` is
   * set and no message is available.
   */
  recv(result: Received, flags?: RecvFlags): boolean;
  /**
   * Register a callback invoked when the socket can accept more sends after
   * back-pressure. The callback runs on a background dispatch thread.
   */
  setSendReadyHandler(handler: () => void): void;
  /**
   * Set the routing id that identifies this ROUTER to its peers. Apply before
   * connecting so peers observe it from the first message.
   */
  setRoutingId(routingId: RoutingId): void;
  /** Return the routing id that identifies this ROUTER to its peers. */
  getRoutingId(): RoutingId;
  /** Begin a request to peer `peerRid`; parts are consumed on submit and a reply is awaited. */
  request(peerRid: RoutingId): RequestOperation;
  /** Begin a reply to request `requestSeq` from `peerRid`; parts are consumed on a successful submit. */
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOperation;
}
