import type { ZLinkMessageSerializer } from './ZLinkMessageSerializer';

export interface ZLinkCodecRegistryBuilder {
  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
  addJson(): this;
  addMessagePack(): this;
  addProtobuf(): this;
}
