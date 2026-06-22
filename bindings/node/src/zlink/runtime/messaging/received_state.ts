// SPDX-License-Identifier: MPL-2.0

import { Received } from '../../contracts/messaging/received';
import { Message } from '../../contracts/messaging/message';
import type { ReplyOperation, SendOperation } from '../../contracts/messaging/operations';
import { RoutingId } from '../../contracts/core/routing_id';

export interface ReplyContext {
  beginReply(): ReplyOperation;
}

export interface SendContext {
  beginSend(): SendOperation;
}

interface ReceivedState {
  parts: Message[];
  routingId: RoutingId | null;
  spotRid: RoutingId | null;
  requestSeq: bigint | null;
  _replyContext: ReplyContext | null;
  _sendContext: SendContext | null;
}

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function freezeOwnedMessageParts(parts: Message[]): Message[] {
  return Object.freeze(parts) as Message[];
}

function closeMessageParts(parts: readonly Message[] | undefined): void {
  if (!parts) {
    return;
  }
  for (const part of parts) {
    part.close();
  }
}

export function createReceived(
  parts: readonly Message[],
  routingId: RoutingId | null = null,
  requestSeq: bigint | null = null,
  spotRid: RoutingId | null = null,
  replyContext: ReplyContext | null = null,
  sendContext: SendContext | null = null
): Received {
  const received = new Received();
  replaceReceived(
    received,
    freezeMessageParts(parts),
    routingId,
    requestSeq,
    spotRid,
    replyContext,
    sendContext
  );
  return received;
}

export function replaceReceived(
  target: Received,
  parts: Message[],
  routingId: RoutingId | null = null,
  requestSeq: bigint | null = null,
  spotRid: RoutingId | null = null,
  replyContext: ReplyContext | null = null,
  sendContext: SendContext | null = null
): void {
  const state = target as unknown as ReceivedState;
  // Caller-provided receive storage is reused in perf and long-running
  // consumers. Release the previous payload before adopting the replacement
  // so native-backed external Buffers do not wait for GC to drop RSS.
  if (state.parts !== parts) {
    closeMessageParts(state.parts);
  }
  state.parts = Object.isFrozen(parts) ? parts : freezeOwnedMessageParts(parts);
  state.routingId = routingId;
  state.spotRid = spotRid;
  state.requestSeq = requestSeq;
  state._replyContext = replyContext;
  state._sendContext = sendContext;
}
