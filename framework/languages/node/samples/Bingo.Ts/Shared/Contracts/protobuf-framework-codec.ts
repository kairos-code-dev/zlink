import { createZlinkProtobufEnvelopeCodec } from '@zlink-systems/framework-codec-protobuf/framework';
import { bingoProtobufOptions } from './protobuf-codec';

export const bingoFrameworkProtobuf = createZlinkProtobufEnvelopeCodec(bingoProtobufOptions);
