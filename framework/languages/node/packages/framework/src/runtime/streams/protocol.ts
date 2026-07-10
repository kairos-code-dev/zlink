import type { Message } from '../../contracts/Common/Message';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import {
  decodeStreamWireFrame,
  decodeStreamWireHeader,
  encodeStreamWireFrame,
  encodeStreamWireHeader,
  lz4PickleUncompressed,
  lz4UnpicklePayload,
  tryDecodeStreamWireFrame
} from '@zlink-systems/stream-wire';

export { utf8Decode, utf8Encode } from '@zlink-systems/stream-wire';

const defaultMaxDecompressedPayloadSize = 64 * 1024;

export enum ZLinkStreamCodec {
  Raw = 0,
  Json = 1,
  MessagePack = 2,
  Protobuf = 3
}

export enum ZLinkStreamMessageKind {
  Send = 1,
  Request = 2,
  Response = 3,
  Error = 4,
  Control = 5
}

export enum ZLinkStreamHeaderFlags {
  None = 0,
  HasRequestSeq = 0x01,
  HasMetadata = 0x02,
  PayloadCompressed = 0x04,
  HasCorrelationId = 0x08
}

export interface ZLinkStreamFrameHeader {
  readonly kind: ZLinkStreamMessageKind;
  readonly codec: ZLinkStreamCodec;
  readonly flags: ZLinkStreamHeaderFlags;
  readonly requestSeq?: bigint;
  readonly name: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly correlationId?: string;
}

export type ZLinkStreamReplyMessageKind =
  | ZLinkStreamMessageKind.Response
  | ZLinkStreamMessageKind.Error;

export interface ZLinkStreamFrame {
  readonly header: Uint8Array;
  readonly payload: Uint8Array;
}

export function resolvePacketName(message: unknown, explicitPacketName: string | undefined): string {
  const packetName = resolveFrameworkPacketName(message, explicitPacketName, 'Stream');
  if (packetName.trim().length === 0) {
    throw new Error('Stream packet name must not be empty.');
  }
  return packetName;
}

export function ensureSingleSubmit(executed: boolean): void {
  if (executed) {
    throw new Error('Stream send builders can be executed only once.');
  }
}

export function encodeStreamFrame(header: ZLinkStreamFrameHeader, payload: Uint8Array): Uint8Array {
  return encodeStreamWireFrame(encodeStreamHeader(header), payload);
}

export function decodeStreamFrame(frame: Uint8Array): ZLinkStreamFrame {
  return decodeStreamWireFrame(frame);
}

export function tryDecodeStreamFrame(frame: Uint8Array): ZLinkStreamFrame | undefined {
  return tryDecodeStreamWireFrame(frame);
}

export function encodeStreamHeader(header: ZLinkStreamFrameHeader): Uint8Array {
  const hasRequestSeq = header.requestSeq !== undefined;
  const hasMetadata = header.metadata.size > 0;
  const hasCorrelation = header.correlationId !== undefined && header.correlationId.length > 0;
  if (header.kind === ZLinkStreamMessageKind.Control && (hasCorrelation || hasRequestSeq || hasMetadata)) {
    throw new Error('Control packet must not contain a request sequence, metadata, or correlation id.');
  }
  return encodeStreamWireHeader(header);
}

export function decodeStreamHeader(header: Uint8Array): ZLinkStreamFrameHeader {
  const decoded = decodeStreamWireHeader(header);
  const kind = decoded.kind as ZLinkStreamMessageKind;
  const flags = decoded.flags as ZLinkStreamHeaderFlags;
  const hasRequestSeq = (flags & ZLinkStreamHeaderFlags.HasRequestSeq) !== 0;
  const hasMetadata = (flags & ZLinkStreamHeaderFlags.HasMetadata) !== 0;
  const hasCorrelation = (flags & ZLinkStreamHeaderFlags.HasCorrelationId) !== 0;
  if (kind === ZLinkStreamMessageKind.Control && (hasCorrelation || hasRequestSeq || hasMetadata)) {
    throw new Error('Control packet must not contain a request sequence, metadata, or correlation id.');
  }
  return {
    kind,
    codec: decoded.codec as ZLinkStreamCodec,
    flags,
    requestSeq: decoded.requestSeq,
    name: decoded.name,
    metadata: decoded.metadata,
    correlationId: decoded.correlationId
  };
}

export function tryGetStreamFrameHeader(header: unknown): ZLinkStreamFrameHeader | undefined {
  if (typeof header !== 'object' || header === null) {
    return undefined;
  }
  const value = header as {
    kind?: unknown;
    codec?: unknown;
    flags?: unknown;
    requestSeq?: unknown;
    name?: unknown;
    metadata?: { values?: ReadonlyMap<string, string> };
    correlationId?: unknown;
  };
  if (
    typeof value.kind !== 'number'
    || typeof value.codec !== 'number'
    || typeof value.flags !== 'number'
    || typeof value.name !== 'string'
  ) {
    return undefined;
  }
  return {
    kind: value.kind as ZLinkStreamMessageKind,
    codec: value.codec as ZLinkStreamCodec,
    flags: value.flags as ZLinkStreamHeaderFlags,
    requestSeq: typeof value.requestSeq === 'bigint' ? value.requestSeq : undefined,
    name: value.name,
    metadata: value.metadata?.values ?? new Map(),
    correlationId: typeof value.correlationId === 'string' ? value.correlationId : undefined
  };
}

export function requireStreamFrameHeader(header: unknown): ZLinkStreamFrameHeader {
  const parsed = tryGetStreamFrameHeader(header);
  if (parsed === undefined) {
    throw new Error('Stream relay requires a decoded stream header.');
  }
  return parsed;
}

export function createStreamReplyHeader(
  requestHeader: ZLinkStreamFrameHeader,
  kind: ZLinkStreamReplyMessageKind,
  codec: ZLinkStreamCodec,
  flags: ZLinkStreamHeaderFlags,
  metadata: ReadonlyMap<string, string>
): ZLinkStreamFrameHeader {
  if (requestHeader.requestSeq === undefined) {
    throw new Error('Stream reply requires a request sequence.');
  }
  return {
    kind,
    codec,
    flags,
    requestSeq: requestHeader.requestSeq,
    name: requestHeader.name,
    metadata,
    correlationId: requestHeader.correlationId
  };
}

export function messageToBytes(message: Message): Uint8Array {
  const value = message as unknown as {
    toBytes?: () => Uint8Array;
    data?: () => Uint8Array;
    bytes?: Uint8Array;
  };
  if (value.toBytes !== undefined) {
    return value.toBytes();
  }
  if (value.data !== undefined) {
    return new Uint8Array(value.data());
  }
  if (value.bytes !== undefined) {
    return new Uint8Array(value.bytes);
  }
  throw new Error('Stream payload cannot be copied for relay.');
}

export function lz4Pickle(payload: Uint8Array): Uint8Array {
  return lz4PickleUncompressed(payload);
}

export function lz4Unpickle(payload: Uint8Array, maxDecompressedSize = defaultMaxDecompressedPayloadSize): Uint8Array {
  return lz4UnpicklePayload(payload, maxDecompressedSize);
}
