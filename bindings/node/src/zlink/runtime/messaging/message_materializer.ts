// SPDX-License-Identifier: MPL-2.0

import { SendFlags } from '../../contracts/sockets/socket_constants';
import {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  type MessageSnapshot
} from '../../contracts/messaging/message';
import { wrapRoutingId } from '../../contracts/service/models';

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

export function materializeReceived(
  raw: NativeReceivedRaw,
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void,
  send?: (parts: readonly Message[], flags: SendFlags) => boolean
): Received {
  const requestSeq = raw.requestSeq ?? null;
  return Received.create(
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null),
    requestSeq,
    wrapRoutingId(raw.spotRid ?? null),
    requestSeq !== null && reply
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
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null),
    requestSeq,
    wrapRoutingId(raw.spotRid ?? null),
    requestSeq !== null && reply
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
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null)
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
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null)
  );
}
