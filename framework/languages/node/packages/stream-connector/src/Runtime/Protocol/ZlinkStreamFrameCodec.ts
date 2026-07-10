import { ZlinkStreamErrorCode } from '../../Contracts';
import { decodeStreamWireFrame, encodeStreamWireFrame } from '@zlink-systems/stream-wire';
import { connectorError } from '../ZlinkStreamSupport';

export class ZlinkStreamFrameCodec {
  static encode(header: Uint8Array, payload: Uint8Array, maxPayloadSize = 64 * 1024): Uint8Array {
    validatePayload(payload.length, maxPayloadSize);
    try {
      return encodeStreamWireFrame(header, payload);
    } catch (cause) {
      throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Frame is too large.', cause);
    }
  }

  static decode(frame: Uint8Array): { header: Uint8Array; payload: Uint8Array } {
    try {
      return decodeStreamWireFrame(frame);
    } catch (cause) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame length does not match prefix.', cause);
    }
  }
}

function validatePayload(payloadLength: number, maxPayloadSize: number): void {
  if (payloadLength > maxPayloadSize) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Payload exceeds MaxSendPayloadSize.');
  }
}
