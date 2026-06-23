import { Message, type MessageLike } from '@zlink-systems/zlink';
import { randomUUID } from 'node:crypto';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkEncodedPayload,
  type ZLinkMessageSerializer
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import { selectDefaultSerializer } from '../messaging/payload-codec';

export const JSON_CONTENT_TYPE = 'application/json';
export const BINARY_CONTENT_TYPE = 'application/octet-stream';

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

export interface ZLinkChannelEnvelopeCodecRegistry {
  readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export function newChannelCorrelationId(): string {
  return randomUUID().replaceAll('-', '');
}

export function encodeChannelEnvelopeParts(
  kind: ZLinkChannelMessageKind,
  channelName: string,
  packetName: string | undefined,
  payload: unknown,
  timeoutMs?: number,
  topic?: string,
  codecs?: ZLinkChannelEnvelopeCodecRegistry,
  correlationId?: string
): readonly MessageLike[] {
  const encoded = encodePayload(payload, codecs);
  const header: ZLinkChannelEnvelopeHeader = {
    kind,
    channelName,
    messageName: resolveFrameworkPacketName(payload, packetName, 'Channel'),
    contentType: encoded.contentType,
    correlationId: correlationId ?? newChannelCorrelationId(),
    deadline: timeoutMs === undefined ? null : new Date(Date.now() + timeoutMs).toISOString(),
    topic: topic ?? null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), encoded.message];
}

export function encodeChannelPublishEnvelopeParts(
  channelName: string,
  topic: string,
  packetName: string | undefined,
  payload: unknown,
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): readonly MessageLike[] {
  const encoded = encodePayload(payload, codecs);
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Publish,
    channelName,
    messageName: resolveFrameworkPacketName(payload, packetName, 'Channel'),
    contentType: encoded.contentType,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: null,
    topic,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), encoded.message];
}

export function encodeChannelReplyParts(
  request: ZLinkChannelEnvelopeHeader,
  payload: unknown,
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): readonly MessageLike[] {
  const encoded = encodePayload(payload ?? Buffer.alloc(0), codecs);
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Response,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: encoded.contentType,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), encoded.message];
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

export function decodeChannelReply<TReply>(
  parts: readonly Message[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): TReply {
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
  if (header.contentType === BINARY_CONTENT_TYPE) {
    return ZLinkEncodedPayload.from(parts[1].data()) as TReply;
  }
  const serializer = codecs?.serializers.get(header.contentType);
  if (serializer !== undefined) {
    return serializer.deserialize<TReply>(ZLinkEncodedPayload.from(parts[1].data()), Object as never);
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

export function decodeChannelPayload(
  envelope: ZLinkChannelEnvelope,
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): unknown {
  try {
    const serializer = codecs?.serializers.get(envelope.header.contentType);
    if (serializer !== undefined) {
      return serializer.deserialize(ZLinkEncodedPayload.from(envelope.payload), Object as never);
    }
    if (envelope.header.contentType === BINARY_CONTENT_TYPE) {
      return ZLinkEncodedPayload.from(envelope.payload);
    }
    if (envelope.header.contentType === JSON_CONTENT_TYPE) {
      return parseWireJson(envelope.payload.toString());
    }
    return ZLinkEncodedPayload.from(envelope.payload);
  } catch (error) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.PayloadDecodeFailed,
      `PayloadDecodeFailed: failed to decode channel payload for '${envelope.header.channelName}:${envelope.header.messageName}'.`,
      false,
      error
    );
  }
}

export function closeMessages(parts: readonly MessageLike[]): void {
  for (const part of parts) {
    if (typeof (part as { close?: unknown }).close === 'function') {
      (part as Message).close();
    }
  }
}

function encodePayload(value: unknown, codecs: ZLinkChannelEnvelopeCodecRegistry | undefined): {
  readonly contentType: string;
  readonly message: MessageLike;
} {
  const serializer = selectDefaultSerializer(codecs);
  if (Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    throw new ZLinkConfigurationException(
      'Channel payloads must be DTOs or ZLinkEncodedPayload values produced by a codec extension; raw Message/Buffer payloads are not part of the framework channel API.'
    );
  }
  if (serializer !== undefined) {
    const contentType = requireDefaultSerializerContentType(codecs, serializer);
    return { contentType, message: serializer.serialize(value).data() };
  }
  return { contentType: contentTypeOf(value), message: toMessageLike(value) };
}

function toMessageLike(value: unknown): MessageLike {
  return encodeJsonBytes(value);
}

function contentTypeOf(value: unknown): string {
  return JSON_CONTENT_TYPE;
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

function requireDefaultSerializerContentType(
  codecs: ZLinkChannelEnvelopeCodecRegistry | undefined,
  serializer: ZLinkMessageSerializer
): string {
  if (codecs === undefined) {
    throw new ZLinkConfigurationException('Channel payload serializer registry is unavailable.');
  }
  for (const [contentType, registered] of codecs.serializers.entries()) {
    if (registered === serializer) {
      return contentType;
    }
  }
  throw new ZLinkConfigurationException('Channel payload serializer is not registered under a content type.');
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
  if (contentType.trim().length === 0) {
    throw new ZLinkConfigurationException('Channel envelope contentType must not be empty.');
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
