import type { Message, MessageLike } from '@zlink-systems/zlink';
import { randomUUID } from 'node:crypto';
import { ZLinkConfigurationException } from '../configuration';

const JSON_CONTENT_TYPE = 'application/json';

const enum ZLinkChannelMessageKind {
  Publish = 4
}

interface ZLinkChannelEnvelopeHeader {
  readonly kind: ZLinkChannelMessageKind;
  readonly channelName: string;
  readonly messageName: string;
  readonly contentType: string;
  readonly correlationId: string | null;
  readonly deadline: string | null;
  readonly topic: string | null;
  readonly errorCode: string | null;
  readonly errorMessage: string | null;
}

export function encodeChannelPublishEnvelopeParts(
  channelName: string,
  topic: string,
  packetName: string | undefined,
  payload: unknown
): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Publish,
    channelName,
    messageName: packetName ?? resolveChannelPacketName(payload),
    contentType: JSON_CONTENT_TYPE,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: null,
    topic,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload)];
}

function toMessageLike(value: unknown): MessageLike {
  if (Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    return value;
  }
  return encodeJsonBytes(value);
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object' && value !== null && typeof (value as { data?: unknown }).data === 'function';
}

function encodeJsonBytes(value: unknown): Buffer {
  return Buffer.from(JSON.stringify(value ?? null));
}

function resolveChannelPacketName(payload: unknown): string {
  if (payload !== null && typeof payload === 'object' && 'constructor' in payload) {
    const name = (payload as { constructor?: { name?: string } }).constructor?.name;
    if (name !== undefined && name !== 'Object') {
      return name;
    }
  }
  throw new ZLinkConfigurationException('Channel packetName is required when the payload type cannot provide one.');
}
