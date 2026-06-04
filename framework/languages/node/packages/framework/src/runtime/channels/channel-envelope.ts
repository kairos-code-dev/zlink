import type { Message, MessageLike } from '@zlink-systems/zlink';
import { randomUUID } from 'node:crypto';
import { ZLinkConfigurationException } from '../configuration';

export const JSON_CONTENT_TYPE = 'application/json';

export const enum ZLinkChannelMessageKind {
  Request = 1,
  Response = 2,
  Command = 3,
  Publish = 4,
  Error = 5
}

export interface ZLinkChannelEnvelopeHeader {
  readonly kind: ZLinkChannelMessageKind;
  readonly channelName: string;
  readonly messageName: string;
  readonly contentType: string;
  readonly correlationId: string | null;
  readonly deadline: string | null;
  readonly topic: string | null;
  readonly errorCode: string | null;
  readonly errorMessage: string | null;
  readonly source?: string | null;
}

export interface ZLinkChannelEnvelope {
  readonly packetName?: string;
  readonly payload: Buffer;
  readonly header: ZLinkChannelEnvelopeHeader;
}

export function encodeChannelEnvelopeParts(
  kind: ZLinkChannelMessageKind,
  channelName: string,
  packetName: string | undefined,
  payload: unknown,
  timeoutMs?: number,
  topic?: string
): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind,
    channelName,
    messageName: packetName ?? resolveChannelPacketName(payload),
    contentType: JSON_CONTENT_TYPE,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: timeoutMs === undefined ? null : new Date(Date.now() + timeoutMs).toISOString(),
    topic: topic ?? null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload)];
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

export function encodeChannelReplyParts(request: ZLinkChannelEnvelopeHeader, payload: unknown): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Response,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload ?? Buffer.alloc(0))];
}

export function encodeChannelErrorReplyParts(request: ZLinkChannelEnvelopeHeader, message: string): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Error,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: 'ZLinkRouteHandlerError',
    errorMessage: message
  };
  return [encodeJsonBytes(header), encodeJsonBytes(null)];
}

export function decodeChannelReply<TReply>(parts: readonly Message[]): TReply {
  const header = decodeChannelHeader(parts);
  if (header.kind === ZLinkChannelMessageKind.Error) {
    throw new ZLinkConfigurationException(header.errorMessage ?? 'ZLink channel request failed.');
  }
  if (header.kind !== ZLinkChannelMessageKind.Response) {
    throw new ZLinkConfigurationException(`Channel reply kind '${header.kind}' is not a response.`);
  }
  if (parts.length < 2 || parts[1].data().length === 0) {
    return undefined as TReply;
  }
  return JSON.parse(parts[1].data().toString()) as TReply;
}

export function decodeChannelEnvelope(parts: readonly Message[]): ZLinkChannelEnvelope {
  const header = decodeChannelHeader(parts);
  if (parts.length < 2) {
    throw new ZLinkConfigurationException('Channel envelope body part is missing.');
  }
  return { header, packetName: header.messageName, payload: Buffer.from(parts[1].data()) };
}

export function closeMessages(parts: readonly Message[]): void {
  for (const part of parts) {
    part.close();
  }
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

function decodeChannelHeader(parts: readonly Message[]): ZLinkChannelEnvelopeHeader {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Channel envelope header part is missing.');
  }
  return JSON.parse(parts[0].data().toString()) as ZLinkChannelEnvelopeHeader;
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
