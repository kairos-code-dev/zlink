import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import type { ZlinkStreamEncodedPayload } from '@zlink-systems/stream-connector';

const bingoProtobuf = zlinkProtobufCodec();

function bingoPayloadBase64(value: unknown, _packetName?: string): string {
  return Buffer.from(bingoProtobuf.encode(value).payload).toString('base64');
}

function encodeBingoPayload(value: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
  return bingoProtobuf.encode(value, messageType);
}

function decodeBingoPayload<TValue = unknown>(payload: ZlinkStreamEncodedPayload): TValue {
  return bingoProtobuf.decode<TValue>(payload);
}

export {
  bingoProtobuf,
  bingoPayloadBase64,
  decodeBingoPayload,
  encodeBingoPayload
};
