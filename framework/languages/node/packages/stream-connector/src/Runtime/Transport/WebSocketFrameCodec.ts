import * as crypto from 'node:crypto';
import { ZlinkStreamErrorCode } from '../../Contracts';
import { connectorError, readUInt16BE, readUInt32BE } from '../ZlinkStreamSupport';
import { BufferedByteQueue } from './BufferedByteQueue';

export interface WebSocketFrame {
  readonly fin: boolean;
  readonly opcode: number;
  readonly payload: Uint8Array;
}

export function tryDecodeWebSocketFrame(buffer: BufferedByteQueue, maxPayloadSize: number): WebSocketFrame | undefined {
  if (buffer.size < 2) {
    return undefined;
  }
  const firstTwo = buffer.peek(2);
  const fin = (firstTwo[0] & 0x80) !== 0;
  const opcode = firstTwo[0] & 0x0f;
  const masked = (firstTwo[1] & 0x80) !== 0;
  let payloadLength = firstTwo[1] & 0x7f;
  let headerLength = 2;
  if (payloadLength === 126) {
    if (buffer.size < 4) {
      return undefined;
    }
    const extended = buffer.peek(4);
    payloadLength = readUInt16BE(extended, 2);
    headerLength = 4;
  } else if (payloadLength === 127) {
    if (buffer.size < 10) {
      return undefined;
    }
    const extended = buffer.peek(10);
    const high = readUInt32BE(extended, 2);
    const low = readUInt32BE(extended, 6);
    if (high !== 0 || low > Number.MAX_SAFE_INTEGER) {
      throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket frame is too large.');
    }
    payloadLength = low;
    headerLength = 10;
  }
  if (payloadLength > maxPayloadSize) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket payload exceeds MaxReceivePayloadSize.');
  }

  const maskLength = masked ? 4 : 0;
  const frameLength = headerLength + maskLength + payloadLength;
  if (buffer.size < frameLength) {
    return undefined;
  }

  const raw = buffer.consume(frameLength);
  const mask = masked ? raw.subarray(headerLength, headerLength + 4) : undefined;
  const payloadStart = headerLength + maskLength;
  const payload = raw.slice(payloadStart);
  if (mask !== undefined) {
    for (let index = 0; index < payload.length; index += 1) {
      payload[index] ^= mask[index % 4];
    }
  }
  return { fin, opcode, payload };
}

export function encodeWebSocketFrame(payload: Uint8Array, options: { opcode: number; masked: boolean }): Buffer {
  const length = payload.length;
  const headerLength = length < 126 ? 2 : length <= 0xffff ? 4 : 10;
  const maskLength = options.masked ? 4 : 0;
  const output = Buffer.alloc(headerLength + maskLength + length);
  output[0] = 0x80 | options.opcode;
  if (length < 126) {
    output[1] = length;
  } else if (length <= 0xffff) {
    output[1] = 126;
    output.writeUInt16BE(length, 2);
  } else {
    output[1] = 127;
    output.writeUInt32BE(0, 2);
    output.writeUInt32BE(length, 6);
  }
  if (!options.masked) {
    Buffer.from(payload).copy(output, headerLength);
    return output;
  }

  output[1] |= 0x80;
  const mask = crypto.randomBytes(4);
  mask.copy(output, headerLength);
  for (let index = 0; index < length; index += 1) {
    output[headerLength + maskLength + index] = payload[index] ^ mask[index % 4];
  }
  return output;
}

export function concatParts(parts: Uint8Array[], maxPayloadSize: number): Uint8Array {
  const length = parts.reduce((sum, part) => sum + part.length, 0);
  if (length > maxPayloadSize) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket message exceeds MaxReceivePayloadSize.');
  }
  const output = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) {
    output.set(part, offset);
    offset += part.length;
  }
  return output;
}
