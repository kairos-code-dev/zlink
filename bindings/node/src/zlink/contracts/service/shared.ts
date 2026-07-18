// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Message } from '../messaging';
import type { MonitorEvent } from '../eventing';

// Re-export the fluent messaging operation contracts so the raw socket layer can
// keep importing them from the `contracts/service` barrel.
export type * from '../messaging/operations';

/** Invoked when a socket can accept more sends after back-pressure. */
export type SocketSendReadyHandler = () => void;

/** Invoked when a spot can accept more sends after back-pressure. */
export type SpotSendReadyHandler = () => void;

/** Invoked for each inbound framed packet with the sender routing id, header, and body; it owns the messages. */
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;

/** Invoked for each socket monitor event. */
export type SocketMonitorHandler = (event: MonitorEvent) => void;

/** One active subscription: a topic filter and whether it is a pattern. */
export interface SubscriptionEntry {
  readonly filter: string;
  readonly isPattern: boolean;
}

/** Optional metadata and flags accepted by send/request/publish operations. */
export interface MessagingOptions {
  /** Application metadata carried alongside the message parts. */
  readonly metadata?: Buffer;
  /** Send flags (e.g. non-blocking submission). */
  readonly flags?: number;
}

/** {@link MessagingOptions} plus a request timeout. */
export interface RequestOptions extends MessagingOptions {
  /** Request timeout in milliseconds; omit or 0 for the node default. */
  readonly timeoutMs?: number;
}
