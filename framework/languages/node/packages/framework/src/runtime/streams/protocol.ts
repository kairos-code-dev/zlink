import type {
  Message,
  ZlinkStreamHeader
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { resolveFrameworkPacketName } from '../messaging/packet-name';

const defaultMaxDecompressedPayloadSize = 64 * 1024;

export enum ZLinkStreamCodec {
  Json = 1
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
  PayloadCompressed = 0x04
}

export interface ZLinkStreamFrameHeader {
  readonly kind: ZLinkStreamMessageKind;
  readonly codec: ZLinkStreamCodec;
  readonly flags: ZLinkStreamHeaderFlags;
  readonly requestSeq?: bigint;
  readonly name: string;
  readonly metadata: ReadonlyMap<string, string>;
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
  const headerBytes = encodeStreamHeader(header);
  if (headerBytes.length > 0xffff) {
    throw new Error('Stream header is too large.');
  }
  if (payload.length > 0xffffffff) {
    throw new Error('Stream payload is too large.');
  }
  const frame = new Uint8Array(6 + headerBytes.length + payload.length);
  writeUInt16BE(frame, 0, headerBytes.length);
  writeUInt32BE(frame, 2, payload.length);
  frame.set(headerBytes, 6);
  frame.set(payload, 6 + headerBytes.length);
  return frame;
}

export function encodeStreamHeader(header: ZLinkStreamFrameHeader): Uint8Array {
  validateStreamPacketName(header.name);
  const nameBytes = utf8Encode(header.name);
  const hasRequestSeq = header.requestSeq !== undefined;
  const hasMetadata = header.metadata.size > 0;
  let flags = header.flags;
  flags = hasRequestSeq ? flags | ZLinkStreamHeaderFlags.HasRequestSeq : flags & ~ZLinkStreamHeaderFlags.HasRequestSeq;
  flags = hasMetadata ? flags | ZLinkStreamHeaderFlags.HasMetadata : flags & ~ZLinkStreamHeaderFlags.HasMetadata;

  const metadataBytes = hasMetadata ? encodeStreamMetadata(header.metadata) : new Uint8Array();
  const size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.length + (hasMetadata ? 2 + metadataBytes.length : 0);
  const buffer = new Uint8Array(size);
  let offset = 0;
  buffer[offset++] = header.kind;
  buffer[offset++] = header.codec;
  buffer[offset++] = flags;
  if (hasRequestSeq) {
    if (header.requestSeq === 0n) {
      throw new Error('Request sequence must not be zero.');
    }
    writeBigUInt64BE(buffer, offset, header.requestSeq);
    offset += 8;
  }
  buffer[offset++] = nameBytes.length;
  buffer.set(nameBytes, offset);
  offset += nameBytes.length;
  if (hasMetadata) {
    writeUInt16BE(buffer, offset, metadataBytes.length);
    offset += 2;
    buffer.set(metadataBytes, offset);
  }
  return buffer;
}

export function decodeStreamHeader(header: Uint8Array): ZLinkStreamFrameHeader {
  let offset = 0;
  if (header.length < 4) {
    throw new Error('Stream header is incomplete.');
  }
  const kind = header[offset++] as ZLinkStreamMessageKind;
  const codec = header[offset++] as ZLinkStreamCodec;
  const flags = header[offset++] as ZLinkStreamHeaderFlags;
  const hasRequestSeq = (flags & ZLinkStreamHeaderFlags.HasRequestSeq) !== 0;
  const hasMetadata = (flags & ZLinkStreamHeaderFlags.HasMetadata) !== 0;
  let requestSeq: bigint | undefined;
  if (hasRequestSeq) {
    if (header.length - offset < 8) {
      throw new Error('Stream request sequence is incomplete.');
    }
    requestSeq = readBigUInt64BE(header, offset);
    offset += 8;
  }
  const nameLength = header[offset++];
  if (nameLength === 0 || header.length - offset < nameLength) {
    throw new Error('Stream packet name is invalid.');
  }
  const name = utf8Decode(header.subarray(offset, offset + nameLength));
  offset += nameLength;
  const decodedMetadata = hasMetadata
    ? decodeStreamMetadata(header, offset)
    : { metadata: new Map<string, string>(), offset };
  if (decodedMetadata.offset !== header.length) {
    throw new Error('Stream header has trailing bytes.');
  }
  return {
    kind,
    codec,
    flags,
    requestSeq,
    name,
    metadata: decodedMetadata.metadata
  };
}

export function tryGetStreamFrameHeader(header: ZlinkStreamHeader): ZLinkStreamFrameHeader | undefined {
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
    metadata: value.metadata?.values ?? new Map()
  };
}

export function requireStreamFrameHeader(header: ZlinkStreamHeader): ZLinkStreamFrameHeader {
  const parsed = tryGetStreamFrameHeader(header);
  if (parsed === undefined) {
    throw new Error('Stream relay requires a decoded stream header.');
  }
  return parsed;
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
  if (payload.length === 0) {
    return new Uint8Array();
  }
  // K4os LZ4Pickler accepts uncompressed packages. This preserves the .NET
  // stream wire contract without adding a native compression dependency.
  const pickled = new Uint8Array(payload.length + 1);
  pickled[0] = 0;
  pickled.set(payload, 1);
  return pickled;
}

export function lz4Unpickle(payload: Uint8Array, maxDecompressedSize = defaultMaxDecompressedPayloadSize): Uint8Array {
  if (payload.length === 0) {
    return new Uint8Array();
  }
  const header = payload[0];
  if ((header & 0x07) !== 0) {
    throw new Error('Unexpected LZ4 pickle version.');
  }
  const sizeOfDiff = decodeDiffSize((header >>> 6) & 0x03);
  const dataOffset = 1 + sizeOfDiff;
  if (payload.length < dataOffset) {
    throw new Error('LZ4 pickle header is incomplete.');
  }
  const data = payload.subarray(dataOffset);
  const resultDiff = sizeOfDiff === 0 ? 0 : readLittleEndian(payload, 1, sizeOfDiff);
  const resultLength = data.length + resultDiff;
  if (resultLength > maxDecompressedSize) {
    throw new Error('LZ4 decoded payload exceeds maximum stream payload size.');
  }
  if (resultDiff === 0) {
    return data.slice();
  }
  return decodeLz4Block(data, resultLength);
}

export function utf8Encode(value: string): Uint8Array {
  return new TextEncoder().encode(value);
}

export function utf8Decode(value: Uint8Array): string {
  return new TextDecoder().decode(value);
}

function encodeStreamMetadata(metadata: ReadonlyMap<string, string>): Uint8Array {
  if (metadata.size > 255) {
    throw new Error('Metadata entry count must not exceed 255.');
  }
  let size = 1;
  const encoded = [...metadata].map(([key, value]) => {
    const keyBytes = utf8Encode(key);
    const valueBytes = utf8Encode(value);
    if (keyBytes.length === 0 || keyBytes.length > 255) {
      throw new Error('Metadata key length is invalid.');
    }
    if (valueBytes.length > 0xffff) {
      throw new Error('Metadata value is too large.');
    }
    size += 1 + keyBytes.length + 2 + valueBytes.length;
    return { keyBytes, valueBytes };
  });
  const buffer = new Uint8Array(size);
  let offset = 0;
  buffer[offset++] = metadata.size;
  for (const { keyBytes, valueBytes } of encoded) {
    buffer[offset++] = keyBytes.length;
    buffer.set(keyBytes, offset);
    offset += keyBytes.length;
    writeUInt16BE(buffer, offset, valueBytes.length);
    offset += 2;
    buffer.set(valueBytes, offset);
    offset += valueBytes.length;
  }
  return buffer;
}

function decodeStreamMetadata(header: Uint8Array, offset: number): { metadata: Map<string, string>; offset: number } {
  if (header.length - offset < 2) {
    throw new Error('Stream metadata section is incomplete.');
  }
  const metadataLength = readUInt16BE(header, offset);
  offset += 2;
  if (header.length - offset < metadataLength) {
    throw new Error('Stream metadata payload is incomplete.');
  }
  const end = offset + metadataLength;
  if (offset >= end) {
    throw new Error('Stream metadata entry count is missing.');
  }
  const count = header[offset++];
  const metadata = new Map<string, string>();
  for (let index = 0; index < count; index += 1) {
    if (offset >= end) {
      throw new Error('Stream metadata key length is missing.');
    }
    const keyLength = header[offset++];
    if (keyLength === 0 || end - offset < keyLength) {
      throw new Error('Stream metadata key is invalid.');
    }
    const key = utf8Decode(header.subarray(offset, offset + keyLength));
    offset += keyLength;
    if (end - offset < 2) {
      throw new Error('Stream metadata value length is missing.');
    }
    const valueLength = readUInt16BE(header, offset);
    offset += 2;
    if (end - offset < valueLength) {
      throw new Error('Stream metadata value is incomplete.');
    }
    metadata.set(key, utf8Decode(header.subarray(offset, offset + valueLength)));
    offset += valueLength;
  }
  if (offset !== end) {
    throw new Error('Stream metadata payload has trailing bytes.');
  }
  return { metadata, offset };
}

function validateStreamPacketName(name: string): void {
  const nameBytes = utf8Encode(name);
  if (name.trim().length === 0 || nameBytes.length > 255) {
    throw new Error('Stream packet name is invalid.');
  }
}

function decodeDiffSize(encoded: number): number {
  return encoded === 3 ? 4 : encoded;
}

function readLittleEndian(source: Uint8Array, offset: number, size: number): number {
  if (size === 1) {
    return source[offset];
  }
  if (size === 2) {
    return source[offset] | (source[offset + 1] << 8);
  }
  if (size === 4) {
    return (
      source[offset]
      | (source[offset + 1] << 8)
      | (source[offset + 2] << 16)
      | (source[offset + 3] * 0x1000000)
    );
  }
  throw new Error(`Unexpected LZ4 pickle field size: ${size}`);
}

function decodeLz4Block(source: Uint8Array, resultLength: number): Uint8Array {
  const target = new Uint8Array(resultLength);
  let sourceOffset = 0;
  let targetOffset = 0;

  while (sourceOffset < source.length) {
    const token = source[sourceOffset++];
    const literalLength = readLz4Length(source, token >>> 4, () => sourceOffset++);
    if (source.length - sourceOffset < literalLength) {
      throw new Error('LZ4 literal run is incomplete.');
    }
    if (target.length - targetOffset < literalLength) {
      throw new Error('LZ4 literal run exceeds output size.');
    }
    target.set(source.subarray(sourceOffset, sourceOffset + literalLength), targetOffset);
    sourceOffset += literalLength;
    targetOffset += literalLength;

    if (sourceOffset >= source.length) {
      break;
    }
    if (source.length - sourceOffset < 2) {
      throw new Error('LZ4 match offset is incomplete.');
    }
    const matchOffset = source[sourceOffset] | (source[sourceOffset + 1] << 8);
    sourceOffset += 2;
    if (matchOffset === 0 || matchOffset > targetOffset) {
      throw new Error('LZ4 match offset is invalid.');
    }
    const matchLength = readLz4Length(source, token & 0x0f, () => sourceOffset++) + 4;
    if (target.length - targetOffset < matchLength) {
      throw new Error('LZ4 match run exceeds output size.');
    }
    for (let index = 0; index < matchLength; index += 1) {
      target[targetOffset + index] = target[targetOffset - matchOffset + index];
    }
    targetOffset += matchLength;
  }

  if (targetOffset !== resultLength) {
    throw new Error('LZ4 decoded length does not match pickle header.');
  }
  return target;
}

function readLz4Length(source: Uint8Array, nibble: number, nextOffset: () => number): number {
  let length = nibble;
  if (length !== 15) {
    return length;
  }
  while (true) {
    const offset = nextOffset();
    if (offset >= source.length) {
      throw new Error('LZ4 extended length is incomplete.');
    }
    const value = source[offset];
    length += value;
    if (value !== 255) {
      return length;
    }
  }
}

function writeUInt16BE(buffer: Uint8Array, offset: number, value: number): void {
  buffer[offset] = (value >>> 8) & 0xff;
  buffer[offset + 1] = value & 0xff;
}

function readUInt16BE(buffer: Uint8Array, offset: number): number {
  return (buffer[offset] << 8) | buffer[offset + 1];
}

function writeUInt32BE(buffer: Uint8Array, offset: number, value: number): void {
  buffer[offset] = (value >>> 24) & 0xff;
  buffer[offset + 1] = (value >>> 16) & 0xff;
  buffer[offset + 2] = (value >>> 8) & 0xff;
  buffer[offset + 3] = value & 0xff;
}

function writeBigUInt64BE(buffer: Uint8Array, offset: number, value: bigint): void {
  for (let index = 7; index >= 0; index -= 1) {
    buffer[offset + index] = Number(value & 0xffn);
    value >>= 8n;
  }
}

function readBigUInt64BE(buffer: Uint8Array, offset: number): bigint {
  let value = 0n;
  for (let index = 0; index < 8; index += 1) {
    value = (value << 8n) | BigInt(buffer[offset + index]);
  }
  return value;
}
