import type { ZLinkMessageSerializer } from './ZLinkMessageSerializer';

export interface ZLinkCodecExtension {
  register(codecs: ZLinkCodecRegistryBuilder): void;
}

export interface ZLinkCodecRegistryBuilder {
  use(extension: ZLinkCodecExtension): this;
  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
  addStreamCodec(contentType: string, codec: unknown): this;
  addJson(): this;
}
