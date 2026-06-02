import type {
  Message,
  ZlinkStreamHeader
} from '../../contracts';

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
  const packetName = explicitPacketName ?? inferPacketName(message);
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

export function utf8Encode(value: string): Uint8Array {
  return new TextEncoder().encode(value);
}

export function utf8Decode(value: Uint8Array): string {
  return new TextDecoder().decode(value);
}

function inferPacketName(message: unknown): string {
  if (message !== null && message !== undefined) {
    const constructor = (message as { constructor?: { name?: string } }).constructor;
    if (constructor?.name !== undefined && constructor.name.length > 0) {
      return constructor.name;
    }
  }
  return 'Object';
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

function validateStreamPacketName(name: string): void {
  const nameBytes = utf8Encode(name);
  if (name.trim().length === 0 || nameBytes.length > 255) {
    throw new Error('Stream packet name is invalid.');
  }
}

function writeUInt16BE(buffer: Uint8Array, offset: number, value: number): void {
  buffer[offset] = (value >>> 8) & 0xff;
  buffer[offset + 1] = value & 0xff;
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
