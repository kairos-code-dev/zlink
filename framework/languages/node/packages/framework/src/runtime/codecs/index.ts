import type {
  ZLinkCodecRegistryBuilder,
  ZLinkMessageSerializer
} from '../../contracts';
import {
  createDefaultProtobufMessageSerializer,
  ZLINK_PROTOBUF_CONTENT_TYPE
} from '../../contracts/Codecs/DefaultMessageSerializers';

export class DefaultZLinkCodecRegistryBuilder implements ZLinkCodecRegistryBuilder {
  private readonly serializers = new Map<string, ZLinkMessageSerializer>();
  private readonly codecs = new Set<string>();

  get registeredCodecs(): readonly string[] {
    return [...this.codecs];
  }

  get registeredSerializers(): ReadonlyMap<string, ZLinkMessageSerializer> {
    if (!this.codecs.has('protobuf') || this.serializers.has(ZLINK_PROTOBUF_CONTENT_TYPE)) {
      return this.serializers;
    }
    return new Map([
      ...this.serializers,
      [ZLINK_PROTOBUF_CONTENT_TYPE, createDefaultProtobufMessageSerializer()]
    ]);
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this {
    const normalized = normalizeContentType(contentType);
    this.serializers.set(normalized, serializer);
    this.codecs.add(normalized);
    return this;
  }

  addJson(): this {
    this.codecs.add('json');
    return this;
  }

  addMessagePack(): this {
    this.codecs.add('messagepack');
    return this;
  }

  addProtobuf(): this {
    this.codecs.add('protobuf');
    return this;
  }
}

function normalizeContentType(contentType: string): string {
  const normalized = contentType.trim();
  if (normalized.length === 0) {
    throw new Error('Codec content type must not be empty.');
  }
  return normalized;
}
