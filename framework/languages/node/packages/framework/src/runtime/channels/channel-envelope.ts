import { Message, type MessageLike } from '@zlink-systems/zlink';
import { randomUUID } from 'node:crypto';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkEncodedPayload,
  type ZLinkMessageSerializer,
  type ZLinkFlowOrigin
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import { selectSerializer } from '../messaging/payload-codec';
import { currentOrCreateFlow } from '../diagnostics/flow-context';

export const ZLINK_CHANNEL_FORMAT_MARKER = 0xf2;

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
  readonly formatMarker: number;
  readonly kind: ZLinkChannelMessageKind;
  readonly channelName: string;
  readonly messageName: string;
  readonly contentType: string;
  readonly correlationId: string | null;
  readonly deadline: string | null;
  readonly topic: string | null;
  readonly errorCode?: string | null;
  readonly errorMessage?: string | null;
  readonly source?: string | null;
  readonly flowId?: string;
  readonly flowOrigin?: ZLinkFlowOrigin;
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
  correlationId?: string,
  createFlow = true
): readonly MessageLike[] {
  const encoded = encodePayload(payload, codecs, { packetName });
  const flow = currentOrCreateFlow('Application', createFlow);
  const header: ZLinkChannelEnvelopeHeader = {
    formatMarker: ZLINK_CHANNEL_FORMAT_MARKER,
    kind,
    channelName,
    messageName: resolveFrameworkPacketName(payload, packetName, 'Channel'),
    contentType: encoded.contentType,
    correlationId: correlationId ?? newChannelCorrelationId(),
    deadline: timeoutMs === undefined ? null : new Date(Date.now() + timeoutMs).toISOString(),
    topic: topic ?? null,
    errorCode: null,
    errorMessage: null,
    ...(flow ?? {})
  };
  return [encodeChannelHeader(header), encoded.message];
}

export function encodeChannelPublishEnvelopeParts(
  channelName: string,
  topic: string,
  packetName: string | undefined,
  payload: unknown,
  codecs?: ZLinkChannelEnvelopeCodecRegistry,
  createFlow = true
): readonly MessageLike[] {
  const encoded = encodePayload(payload, codecs, { packetName });
  const flow = currentOrCreateFlow('Application', createFlow);
  const header: ZLinkChannelEnvelopeHeader = {
    formatMarker: ZLINK_CHANNEL_FORMAT_MARKER,
    kind: ZLinkChannelMessageKind.Publish,
    channelName,
    messageName: resolveFrameworkPacketName(payload, packetName, 'Channel'),
    contentType: encoded.contentType,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: null,
    topic,
    errorCode: null,
    errorMessage: null,
    ...(flow ?? {})
  };
  return [encodeChannelHeader(header), encoded.message];
}

export function encodeChannelReplyParts(
  request: ZLinkChannelEnvelopeHeader,
  payload: unknown,
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): readonly MessageLike[] {
  const encoded = encodePayload(payload ?? Buffer.alloc(0), codecs, { packetName: request.messageName });
  const header: ZLinkChannelEnvelopeHeader = {
    formatMarker: ZLINK_CHANNEL_FORMAT_MARKER,
    kind: ZLinkChannelMessageKind.Response,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: encoded.contentType,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    flowId: request.flowId,
    flowOrigin: request.flowOrigin
  };
  return [encodeChannelHeader(header), encoded.message];
}

export function encodeChannelErrorReplyParts(request: ZLinkChannelEnvelopeHeader, error: unknown): readonly MessageLike[] {
  const errorCode = error instanceof ZLinkFrameworkException
    ? error.kind
    : error instanceof Error
      ? error.name
      : typeof error;
  const errorMessage = error instanceof Error ? error.message : String(error);
  const header: ZLinkChannelEnvelopeHeader = {
    formatMarker: ZLINK_CHANNEL_FORMAT_MARKER,
    kind: ZLinkChannelMessageKind.Error,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: errorCode.length > 0 ? errorCode : 'Error',
    errorMessage,
    flowId: request.flowId,
    flowOrigin: request.flowOrigin
  };
  return [encodeChannelHeader(header), encodeJsonBytes(null)];
}

export function decodeChannelReply<TReply>(
  parts: readonly Message[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): TReply {
  const header = decodeChannelHeader(parts);
  if (header.kind === ZLinkChannelMessageKind.Error) {
    throw decodeChannelError(header);
  }
  if (header.kind !== ZLinkChannelMessageKind.Response) {
    throw new ZLinkConfigurationException(`Channel reply kind '${header.kind}' is not a response.`);
  }
  if (parts.length < 2 || parts[1].data().length === 0) {
    return undefined as TReply;
  }
  if (header.contentType === BINARY_CONTENT_TYPE) {
    return Buffer.from(parts[1].data()) as TReply;
  }
  const serializer = codecs?.serializers.get(header.contentType);
  if (serializer !== undefined) {
    return serializer.deserialize<TReply>(ZLinkEncodedPayload.from(parts[1].data()), Object as never);
  }
  return parseWireJson(parts[1].data().toString()) as TReply;
}

const FRAMEWORK_ERROR_KINDS = new Set<string>(Object.values(ZLinkFrameworkErrorKind));

function decodeChannelError(header: ZLinkChannelEnvelopeHeader): Error {
  const code = header.errorCode;
  if (code === undefined || code === null || code.trim().length === 0) {
    return new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RequestProtocolError,
      'Channel Error reply does not contain a non-empty errorCode.'
    );
  }
  const message = header.errorMessage ?? 'ZLink channel request failed.';
  if (FRAMEWORK_ERROR_KINDS.has(code)) {
    return new ZLinkFrameworkException(code as ZLinkFrameworkErrorKind, message);
  }
  switch (code) {
    case 'TypeError': return new TypeError(message);
    case 'RangeError': return new RangeError(message);
    case 'SyntaxError': return new SyntaxError(message);
    default: {
      const error = new Error(message);
      error.name = code;
      return error;
    }
  }
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
      return Buffer.from(envelope.payload);
    }
    if (envelope.header.contentType === JSON_CONTENT_TYPE) {
      return parseWireJson(envelope.payload.toString());
    }
    return Buffer.from(envelope.payload);
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

function encodePayload(
  value: unknown,
  codecs: ZLinkChannelEnvelopeCodecRegistry | undefined,
  context: { readonly packetName?: string } = {}
): {
  readonly contentType: string;
  readonly message: MessageLike;
} {
  const serializer = selectSerializer(value, codecs, context);
  if (serializer !== undefined && !(Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value))) {
    const contentType = requireDefaultSerializerContentType(codecs, serializer);
    return { contentType, message: serializer.serialize(value).data() };
  }
  return { contentType: contentTypeOf(value), message: toMessageLike(value) };
}

function toMessageLike(value: unknown): MessageLike {
  if (Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    return value;
  }
  return encodeJsonBytes(value);
}

function contentTypeOf(value: unknown): string {
  return Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)
    ? BINARY_CONTENT_TYPE
    : JSON_CONTENT_TYPE;
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object' && value !== null && typeof (value as { data?: unknown }).data === 'function';
}

function encodeJsonBytes(value: unknown): Buffer {
  return Buffer.from(JSON.stringify(value ?? null, (_key, item) =>
    typeof item === 'bigint' ? item.toString() : item
  ));
}

function encodeChannelHeader(header: ZLinkChannelEnvelopeHeader): Buffer {
  return encodeJsonBytes({
    ...header,
    flowOrigin: header.flowOrigin === undefined ? undefined : encodeFlowOrigin(header.flowOrigin)
  });
}

function encodeFlowOrigin(origin: ZLinkFlowOrigin): number {
  switch (origin) {
    case 'Inbound': return 1;
    case 'Timer': return 2;
    case 'Application': return 3;
    case 'Lifecycle': return 4;
  }
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
  if (header.formatMarker !== ZLINK_CHANNEL_FORMAT_MARKER) {
    throw new ZLinkConfigurationException('Channel envelope format marker is invalid.');
  }
  const kind = requireChannelMessageKind(header.kind);
  const contentType = requireString(header.contentType, 'contentType');
  if (contentType.trim().length === 0) {
    throw new ZLinkConfigurationException('Channel envelope contentType must not be empty.');
  }
  const flowId = optionalFlowId(header.flowId);
  const flowOrigin = optionalFlowOrigin(header.flowOrigin);
  if ((flowId === undefined) !== (flowOrigin === undefined)) {
    throw new ZLinkConfigurationException('Channel envelope flowId and flowOrigin must both be present or absent.');
  }
  return {
    formatMarker: ZLINK_CHANNEL_FORMAT_MARKER,
    kind,
    channelName: requireString(header.channelName, 'channelName'),
    messageName: requireString(header.messageName, 'messageName'),
    contentType,
    correlationId: requireNullableString(header.correlationId, 'correlationId'),
    deadline: requireNullableString(header.deadline, 'deadline'),
    topic: requireNullableString(header.topic, 'topic'),
    errorCode: header.errorCode === undefined ? null : requireNullableString(header.errorCode, 'errorCode'),
    errorMessage: header.errorMessage === undefined ? null : requireNullableString(header.errorMessage, 'errorMessage'),
    source: header.source === undefined ? undefined : requireNullableString(header.source, 'source'),
    flowId,
    flowOrigin
  };
}

function optionalFlowId(value: unknown): string | undefined {
  return value === undefined ? undefined : requireFlowId(value);
}

function optionalFlowOrigin(value: unknown): ZLinkFlowOrigin | undefined {
  return value === undefined ? undefined : requireFlowOrigin(value);
}

function requireFlowId(value: unknown): string {
  const flowId = requireString(value, 'flowId');
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(flowId)) {
    throw new ZLinkConfigurationException('Channel envelope flowId must be a lowercase UUIDv7.');
  }
  return flowId;
}

function requireFlowOrigin(value: unknown): ZLinkFlowOrigin {
  switch (value) {
    case 1: return 'Inbound';
    case 2: return 'Timer';
    case 3: return 'Application';
    case 4: return 'Lifecycle';
  }
  throw new ZLinkConfigurationException('Channel envelope flowOrigin is invalid.');
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
