import type { ZLinkMessageSerializer } from './ZLinkMessageSerializer';
import type { Type } from '../Common';

export interface ZLinkCodecExtension {
  register(codecs: ZLinkCodecRegistrar): void;
}

export interface ZLinkCodecRegistryBuilder {
  use(extension: ZLinkCodecExtension): this;
}

export interface ZLinkCodecRegistrar {
  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
  addSerializer(
    contentType: string,
    serializer: ZLinkMessageSerializer,
    canSerialize: (payloadType: Type) => boolean
  ): this;
  addStreamCodec(contentType: string, codec: unknown): this;
}
