import type {
  ZLinkCodecRegistryBuilder,
  ZLinkMessageSerializer
} from '../../contracts';

export class DefaultZLinkCodecRegistryBuilder implements ZLinkCodecRegistryBuilder {
  private readonly serializers = new Map<string, ZLinkMessageSerializer>();
  private readonly codecs = new Set<string>();

  get registeredCodecs(): readonly string[] {
    return [...this.codecs];
  }

  get registeredSerializers(): ReadonlyMap<string, ZLinkMessageSerializer> {
    return this.serializers;
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
