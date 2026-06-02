import {
  ZlinkStreamCodec,
  ZlinkStreamErrorCode,
  ZlinkStreamHeaderFlags,
  ZlinkStreamMessageKind,
  ZlinkStreamMetadata,
  ZlinkStreamMetadataMap,
  type ZlinkStreamHeader
} from './contracts';
import {
  connectorError,
  readBigUInt64BE,
  readLength,
  readUInt16BE,
  readUInt32BE,
  utf8Decode,
  utf8Encode,
  writeBigUInt64BE,
  writeUInt16BE,
  writeUInt32BE
} from './support';

export class ZlinkStreamHeaderCodec {
  static encode(header: ZlinkStreamHeader): Uint8Array {
    validateName(header.name, header.kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(header);

    const nameBytes = utf8Encode(header.name);
    const hasRequestSeq = header.requestSeq !== undefined;
    const hasMetadata = header.metadata.count > 0;
    let flags = header.flags;
    flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
    flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;

    const metadataSize = hasMetadata ? ZlinkStreamMetadataCodec.size(header.metadata) : 0;
    const size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.length + (hasMetadata ? 2 + metadataSize : 0);
    const buffer = new Uint8Array(size);
    let offset = 0;
    buffer[offset++] = header.kind;
    buffer[offset++] = header.codec;
    buffer[offset++] = flags;
    if (hasRequestSeq) {
      if (header.requestSeq === 0n) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Request sequence must not be zero.');
      }
      writeBigUInt64BE(buffer, offset, header.requestSeq);
      offset += 8;
    }
    buffer[offset++] = nameBytes.length;
    buffer.set(nameBytes, offset);
    offset += nameBytes.length;
    if (hasMetadata) {
      writeUInt16BE(buffer, offset, metadataSize);
      offset += 2;
      ZlinkStreamMetadataCodec.write(header.metadata, buffer.subarray(offset, offset + metadataSize));
    }
    return buffer;
  }

  static decode(header: Uint8Array): ZlinkStreamHeader {
    if (header.length < 4) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header is too short.');
    }
    let offset = 0;
    const kind = header[offset++] as ZlinkStreamMessageKind;
    const codec = header[offset++] as ZlinkStreamCodec;
    const flags = header[offset++] as ZlinkStreamHeaderFlags;
    validateEnum(kind, codec, flags);

    let requestSeq: bigint | undefined;
    if ((flags & ZlinkStreamHeaderFlags.HasRequestSeq) !== 0) {
      if (header.length - offset < 8) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header request sequence is incomplete.');
      }
      requestSeq = readBigUInt64BE(header, offset);
      if (requestSeq === 0n) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Request sequence must not be zero.');
      }
      offset += 8;
    }

    if (header.length - offset < 1) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header name length is missing.');
    }
    const nameLength = header[offset++];
    if (nameLength === 0 || header.length - offset < nameLength) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header packet name is invalid.');
    }
    const name = utf8Decode(header.subarray(offset, offset + nameLength));
    offset += nameLength;

    let metadata = ZlinkStreamMetadataMap.empty;
    if ((flags & ZlinkStreamHeaderFlags.HasMetadata) !== 0) {
      if (header.length - offset < 2) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header metadata length is missing.');
      }
      const metadataLength = readUInt16BE(header, offset);
      offset += 2;
      if (header.length - offset < metadataLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header metadata is incomplete.');
      }
      metadata = ZlinkStreamMetadataCodec.decode(header.subarray(offset, offset + metadataLength));
      offset += metadataLength;
    }
    if (offset !== header.length) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header contains trailing bytes.');
    }

    const decoded = { kind, codec, flags, requestSeq, name, metadata };
    validateName(name, kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(decoded);
    return decoded;
  }
}

export class ZlinkStreamFrameCodec {
  static encode(header: Uint8Array, payload: Uint8Array, maxPayloadSize = 64 * 1024): Uint8Array {
    validatePayload(payload.length, maxPayloadSize);
    validateFrame(header.length, payload.length);
    const frame = new Uint8Array(6 + header.length + payload.length);
    writeUInt16BE(frame, 0, header.length);
    writeUInt32BE(frame, 2, payload.length);
    frame.set(header, 6);
    frame.set(payload, 6 + header.length);
    return frame;
  }

  static decode(frame: Uint8Array): { header: Uint8Array; payload: Uint8Array } {
    if (frame.length < 6) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame prefix is incomplete.');
    }
    const headerLength = readUInt16BE(frame, 0);
    const payloadLength = readUInt32BE(frame, 2);
    const expected = 6 + headerLength + payloadLength;
    if (frame.length !== expected) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame length does not match prefix.');
    }
    return {
      header: frame.slice(6, 6 + headerLength),
      payload: frame.slice(6 + headerLength)
    };
  }
}

class ZlinkStreamMetadataCodec {
  static size(metadata: ZlinkStreamMetadata): number {
    if (metadata.count === 0) {
      return 0;
    }
    if (metadata.count > 255) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata entry count must not exceed 255.');
    }
    let size = 1;
    for (const [key, value] of metadata.values) {
      const keyLength = utf8Encode(key).length;
      const valueLength = utf8Encode(value).length;
      if (keyLength === 0 || keyLength > 255) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata key length is invalid.');
      }
      if (valueLength > 0xffff) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata value is too large.');
      }
      size += 1 + keyLength + 2 + valueLength;
    }
    return size;
  }

  static write(metadata: ZlinkStreamMetadata, destination: Uint8Array): void {
    let offset = 0;
    destination[offset++] = metadata.count;
    for (const [key, value] of metadata.values) {
      const keyBytes = utf8Encode(key);
      const valueBytes = utf8Encode(value);
      destination[offset++] = keyBytes.length;
      destination.set(keyBytes, offset);
      offset += keyBytes.length;
      writeUInt16BE(destination, offset, valueBytes.length);
      offset += 2;
      destination.set(valueBytes, offset);
      offset += valueBytes.length;
    }
  }

  static decode(metadata: Uint8Array): ZlinkStreamMetadata {
    if (metadata.length === 0) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata payload is empty.');
    }
    let offset = 0;
    const count = metadata[offset++];
    const values = new Map<string, string>();
    for (let i = 0; i < count; i++) {
      const keyLength = readLength(metadata, offset, 1, 'key');
      offset += 1;
      if (keyLength === 0 || metadata.length - offset < keyLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata key is invalid.');
      }
      const key = utf8Decode(metadata.subarray(offset, offset + keyLength));
      offset += keyLength;
      const valueLength = readLength(metadata, offset, 2, 'value');
      offset += 2;
      if (metadata.length - offset < valueLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata value is invalid.');
      }
      if (values.has(key)) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Duplicate metadata key.');
      }
      values.set(key, utf8Decode(metadata.subarray(offset, offset + valueLength)));
      offset += valueLength;
    }
    if (offset !== metadata.length) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata contains trailing bytes.');
    }
    return values.size === 0 ? ZlinkStreamMetadataMap.empty : ZlinkStreamMetadataMap.from(values);
  }
}

export function buildHeader(
  kind: ZlinkStreamMessageKind,
  name: string,
  codec: ZlinkStreamCodec,
  metadata: ZlinkStreamMetadata,
  compress: boolean,
  requestSeq: bigint | undefined
): ZlinkStreamHeader {
  let flags = ZlinkStreamHeaderFlags.None;
  if (requestSeq !== undefined) {
    flags |= ZlinkStreamHeaderFlags.HasRequestSeq;
  }
  if (metadata.count > 0) {
    flags |= ZlinkStreamHeaderFlags.HasMetadata;
  }
  if (compress) {
    flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
  }
  return { kind, codec, flags, requestSeq, name, metadata };
}

export function validateName(name: string, allowReserved = false): void {
  if (name.length === 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name must not be empty.');
  }
  if (!allowReserved && name.startsWith('$zlink.')) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name uses a reserved zlink prefix.');
  }
  if (utf8Encode(name).length > 255) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name must not exceed 255 UTF-8 bytes.');
  }
}

function validateHeaderSemantics(header: ZlinkStreamHeader): void {
  validateEnum(header.kind, header.codec, header.flags);
  const hasRequestSeq = header.requestSeq !== undefined || (header.flags & ZlinkStreamHeaderFlags.HasRequestSeq) !== 0;
  const hasMetadata = header.metadata.count > 0 || (header.flags & ZlinkStreamHeaderFlags.HasMetadata) !== 0;
  if (header.kind === ZlinkStreamMessageKind.Send && hasRequestSeq) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Send packet must not contain a request sequence.');
  }
  if ((header.kind === ZlinkStreamMessageKind.Request || header.kind === ZlinkStreamMessageKind.Response) && !hasRequestSeq) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Request and response packets must contain a request sequence.');
  }
  if (header.kind === ZlinkStreamMessageKind.Error && header.codec !== ZlinkStreamCodec.Json) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Error packet must use the JSON codec.');
  }
  if (header.kind === ZlinkStreamMessageKind.Control) {
    if (header.flags !== ZlinkStreamHeaderFlags.None || hasRequestSeq || hasMetadata || header.codec !== ZlinkStreamCodec.Raw) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Control packet must use raw codec and must not contain flags.');
    }
  }
}

function validateEnum(kind: ZlinkStreamMessageKind, codec: ZlinkStreamCodec, flags: ZlinkStreamHeaderFlags): void {
  if (![1, 2, 3, 4, 5].includes(kind)) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream message kind.');
  }
  if (![0, 1, 2, 3].includes(codec)) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream codec.');
  }
  const known = ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.HasMetadata | ZlinkStreamHeaderFlags.PayloadCompressed;
  if ((flags & ~known) !== 0) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream header flag.');
  }
}

function validateFrame(headerLength: number, payloadLength: number): void {
  if (headerLength > 0xffff) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Header exceeds u16 header_size.');
  }
  if (2 + 4 + headerLength + payloadLength > Number.MAX_SAFE_INTEGER) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Frame is too large.');
  }
}

function validatePayload(payloadLength: number, maxPayloadSize: number): void {
  if (payloadLength > maxPayloadSize) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Payload exceeds MaxSendPayloadSize.');
  }
}
