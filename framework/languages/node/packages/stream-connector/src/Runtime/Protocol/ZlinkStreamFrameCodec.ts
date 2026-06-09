import { ZlinkStreamErrorCode } from '../../Contracts';
import { connectorError, readUInt16BE, readUInt32BE, writeUInt16BE, writeUInt32BE } from '../ZlinkStreamSupport';

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
