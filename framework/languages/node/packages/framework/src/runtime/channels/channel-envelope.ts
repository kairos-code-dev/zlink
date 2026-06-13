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
  return parseWireJson(parts[1].data().toString()) as TReply;
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
  return validateChannelHeader(parseWireJson(parts[0].data().toString()));
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

function parseWireJson(payload: string): unknown {
  return JSON.parse(payload, (key, value) => {
    if (isPrototypeKey(key)) {
      throw new ZLinkConfigurationException(`Channel JSON key '${key}' is not allowed.`);
    }
    return value;
  });
}

function validateChannelHeader(value: unknown): ZLinkChannelEnvelopeHeader {
  if (!isRecord(value)) {
    throw new ZLinkConfigurationException('Channel envelope header must be a JSON object.');
  }
  const header = value as Record<string, unknown>;
  const kind = requireChannelMessageKind(header.kind);
  const contentType = requireString(header.contentType, 'contentType');
  if (contentType !== JSON_CONTENT_TYPE) {
    throw new ZLinkConfigurationException(`Channel envelope contentType '${contentType}' is not supported.`);
  }
  return {
    kind,
    channelName: requireString(header.channelName, 'channelName'),
    messageName: requireString(header.messageName, 'messageName'),
    contentType,
    correlationId: requireNullableString(header.correlationId, 'correlationId'),
    deadline: requireNullableString(header.deadline, 'deadline'),
    topic: requireNullableString(header.topic, 'topic'),
    errorCode: requireNullableString(header.errorCode, 'errorCode'),
    errorMessage: requireNullableString(header.errorMessage, 'errorMessage'),
    source: header.source === undefined ? undefined : requireNullableString(header.source, 'source')
  };
}

function requireChannelMessageKind(value: unknown): ZLinkChannelMessageKind {
  if (
    value === ZLinkChannelMessageKind.Request ||
    value === ZLinkChannelMessageKind.Response ||
    value === ZLinkChannelMessageKind.Command ||
    value === ZLinkChannelMessageKind.Publish ||
    value === ZLinkChannelMessageKind.Error
  ) {
    return value;
  }
  throw new ZLinkConfigurationException('Channel envelope kind is not supported.');
}

function requireString(value: unknown, fieldName: string): string {
  if (typeof value !== 'string') {
    throw new ZLinkConfigurationException(`Channel envelope ${fieldName} must be a string.`);
  }
  return value;
}

function requireNullableString(value: unknown, fieldName: string): string | null {
  if (value === null || typeof value === 'string') {
    return value;
  }
  throw new ZLinkConfigurationException(`Channel envelope ${fieldName} must be a string or null.`);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isPrototypeKey(key: string): boolean {
  return key === '__proto__' || key === 'constructor' || key === 'prototype';
}
