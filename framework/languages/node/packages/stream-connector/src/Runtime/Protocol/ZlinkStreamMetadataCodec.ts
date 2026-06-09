import { ZlinkStreamErrorCode, ZlinkStreamMetadata, ZlinkStreamMetadataMap } from '../../Contracts';
import { connectorError, readLength, utf8Decode, utf8Encode, writeUInt16BE } from '../ZlinkStreamSupport';

export class ZlinkStreamMetadataCodec {
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
