import {
  ZlinkStreamCodec,
  ZlinkStreamErrorCode,
  ZlinkStreamHeaderFlags,
  ZlinkStreamMessageKind,
  ZlinkStreamMetadata,
  ZlinkStreamMetadataMap,
  type ZlinkStreamHeader
} from '../../Contracts';
import {
  connectorError,
  readBigUInt64BE,
  readUInt16BE,
  utf8Decode,
  utf8Encode,
  writeBigUInt64BE,
  writeUInt16BE
} from '../ZlinkStreamSupport';
import { ZlinkStreamMetadataCodec } from './ZlinkStreamMetadataCodec';
import { validateName } from './ZlinkStreamPacketNameValidator';

export class ZlinkStreamHeaderCodec {
  static encode(header: ZlinkStreamHeader): Uint8Array {
    validateName(header.name, header.kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(header);

    const nameBytes = utf8Encode(header.name);
    const hasRequestSeq = header.requestSeq !== undefined;
    const hasMetadata = header.metadata.count > 0;
    const correlationBytes = header.correlationId !== undefined && header.correlationId.length > 0
      ? utf8Encode(header.correlationId)
      : undefined;
    if (correlationBytes !== undefined && correlationBytes.length > 0xff) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Correlation id is too large.');
    }
    const hasCorrelation = correlationBytes !== undefined;
    let flags = header.flags;
    flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
    flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;
    flags = hasCorrelation ? flags | ZlinkStreamHeaderFlags.HasCorrelationId : flags & ~ZlinkStreamHeaderFlags.HasCorrelationId;

    const metadataSize = hasMetadata ? ZlinkStreamMetadataCodec.size(header.metadata) : 0;
    const size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.length
      + (hasMetadata ? 2 + metadataSize : 0)
      + (hasCorrelation ? 1 + correlationBytes.length : 0);
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
      offset += metadataSize;
    }
    if (hasCorrelation) {
      buffer[offset++] = correlationBytes.length;
      buffer.set(correlationBytes, offset);
      offset += correlationBytes.length;
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

    let correlationId: string | undefined;
    if ((flags & ZlinkStreamHeaderFlags.HasCorrelationId) !== 0) {
      if (header.length - offset < 1) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header correlation id length is missing.');
      }
      const correlationLength = header[offset++];
      if (header.length - offset < correlationLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header correlation id is incomplete.');
      }
      correlationId = utf8Decode(header.subarray(offset, offset + correlationLength));
      offset += correlationLength;
    }
    if (offset !== header.length) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header contains trailing bytes.');
    }

    const decoded = { kind, codec, flags, requestSeq, name, metadata, correlationId };
    validateName(name, kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(decoded);
    return decoded;
  }
}

export function buildHeader(
  kind: ZlinkStreamMessageKind,
  name: string,
  codec: ZlinkStreamCodec,
  metadata: ZlinkStreamMetadata,
  compress: boolean,
  requestSeq: bigint | undefined,
  correlationId?: string
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
  if (correlationId !== undefined && correlationId.length > 0) {
    flags |= ZlinkStreamHeaderFlags.HasCorrelationId;
  }
  return { kind, codec, flags, requestSeq, name, metadata, correlationId };
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
    const hasCorrelation = (header.correlationId !== undefined && header.correlationId.length > 0)
      || (header.flags & ZlinkStreamHeaderFlags.HasCorrelationId) !== 0;
    if (header.flags !== ZlinkStreamHeaderFlags.None || hasRequestSeq || hasMetadata || hasCorrelation || header.codec !== ZlinkStreamCodec.Raw) {
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
  const known = ZlinkStreamHeaderFlags.HasRequestSeq
    | ZlinkStreamHeaderFlags.HasMetadata
    | ZlinkStreamHeaderFlags.PayloadCompressed
    | ZlinkStreamHeaderFlags.HasCorrelationId;
  if ((flags & ~known) !== 0) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream header flag.');
  }
}
