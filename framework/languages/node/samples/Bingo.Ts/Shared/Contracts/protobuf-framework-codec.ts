import {
  createZlinkProtobufEnvelopeCodec,
  type ZLinkProtobufEnvelopeCodecExtension
} from '@zlink-systems/framework-codec-protobuf/framework';
import { bingoProtobufOptions } from './protobuf-codec';
import { BingoGeneratedMessageConstructors } from './bingo-messages.generated';

const generatedMessageTypes = new Set<Function>(Object.values(BingoGeneratedMessageConstructors));
const baseCodec = createZlinkProtobufEnvelopeCodec(bingoProtobufOptions);

const bingoFrameworkProtobuf: ZLinkProtobufEnvelopeCodecExtension = {
  register(codecs) {
    baseCodec.register({
      addSerializer(contentType, serializer) {
        return codecs.addSerializer(contentType, {
          ...serializer,
          canSerialize(value: unknown): boolean {
            return value !== null
              && value !== undefined
              && generatedMessageTypes.has((value as { constructor?: Function }).constructor as Function);
          }
        } as typeof serializer & { canSerialize(value: unknown): boolean });
      },
      addStreamCodec(contentType, codec) {
        return codecs.addStreamCodec(contentType, codec);
      }
    });
  },
  encode: baseCodec.encode,
  decode: baseCodec.decode
};

export { bingoFrameworkProtobuf };
