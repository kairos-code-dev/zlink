// SPDX-License-Identifier: MPL-2.0

import { SendFlags } from '../../contracts/sockets/socket_constants';
import {
  Message,
  Received,
  RoutingId,
  TopicMessage
} from '../../contracts';
import {
  messageFromSnapshot,
  type MessageSnapshot
} from './message_snapshot';

export interface NativeReceivedRaw {
  parts: MessageSnapshot[];
  routingId?: Buffer | null;
  requestSeq?: bigint | null;
  spotRid?: Buffer | null;
}

export interface NativeTopicMessageRaw {
  topic: string;
  parts: MessageSnapshot[];
  routingId?: Buffer | null;
}

function wrapNativeRoutingId(routingId: Buffer | null | undefined): RoutingId | null {
  if (!routingId || routingId.length === 0) {
    return null;
  }
  return RoutingId.fromOwnedBuffer(routingId);
}

function materializeParts(parts: MessageSnapshot[]): Message[] {
  if (parts.length === 1) {
    return [messageFromSnapshot(parts[0])];
  }
  const messages = new Array<Message>(parts.length);
  for (let i = 0; i < parts.length; i += 1) {
    messages[i] = messageFromSnapshot(parts[i]);
  }
  return messages;
}

function hasReplyableRequestSeq(requestSeq: bigint | null): requestSeq is bigint {
  return requestSeq !== null && requestSeq !== 0n;
}

export function materializeReceived(
  raw: NativeReceivedRaw,
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void,
  send?: (parts: readonly Message[], flags: SendFlags) => boolean
): Received {
  const requestSeq = raw.requestSeq ?? null;
  return Received.create(
    materializeParts(raw.parts),
    wrapNativeRoutingId(raw.routingId ?? null),
    requestSeq,
    wrapNativeRoutingId(raw.spotRid ?? null),
    hasReplyableRequestSeq(requestSeq) && reply
      ? {
          reply(parts: readonly Message[], flags: SendFlags): void {
            reply(requestSeq, parts, flags);
          }
        }
      : null,
    send
      ? {
          send(parts: readonly Message[], flags: SendFlags): boolean {
            return send(parts, flags);
          }
        }
      : null
  );
}

export function materializeReceivedInto(
  target: Received,
  raw: NativeReceivedRaw,
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void,
  send?: (parts: readonly Message[], flags: SendFlags) => boolean
): void {
  const requestSeq = raw.requestSeq ?? null;
  (target as Received & {
    _replace: (
      parts: Message[],
      routingId: RoutingId | null,
      requestSeq: bigint | null,
      spotRid: RoutingId | null,
      replyContext: unknown,
      sendContext: unknown
    ) => void;
  })._replace(
    materializeParts(raw.parts),
    wrapNativeRoutingId(raw.routingId ?? null),
    requestSeq,
    wrapNativeRoutingId(raw.spotRid ?? null),
    hasReplyableRequestSeq(requestSeq) && reply
      ? {
          reply(parts: readonly Message[], flags: SendFlags): void {
            reply(requestSeq, parts, flags);
          }
        }
      : null,
    send
      ? {
          send(parts: readonly Message[], flags: SendFlags): boolean {
            return send(parts, flags);
          }
        }
      : null
  );
}

export function materializeTopicMessage(raw: NativeTopicMessageRaw): TopicMessage {
  return TopicMessage.create(
    raw.topic,
    materializeParts(raw.parts),
    wrapNativeRoutingId(raw.routingId ?? null)
  );
}

export function adoptTopicMessage(result: TopicMessage, raw: NativeTopicMessageRaw): void {
  (result as TopicMessage & {
    _replace: (
      topic: string,
      parts: Message[],
      routingId: RoutingId | null
    ) => void;
  })._replace(
    raw.topic,
    materializeParts(raw.parts),
    wrapNativeRoutingId(raw.routingId ?? null)
  );
}
