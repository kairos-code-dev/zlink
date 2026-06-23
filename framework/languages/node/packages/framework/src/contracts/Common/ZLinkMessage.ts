import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { Type } from './CoreTypes';
import type { Message } from './Message';
import type { ZLinkMessageSerializer } from '../Codecs';

export interface ZLinkSerializerRegistryLike {
  readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export class ZLinkMessage<TValue = unknown> {
  private constructor(
    private readonly value: TValue | undefined,
    private readonly encoded: Message | undefined,
    private readonly registry: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {}

  static from<T>(value: T): ZLinkMessage<T> {
    return new ZLinkMessage(value, undefined, undefined);
  }

  static fromEncoded(
    message: Message,
    registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
  ): ZLinkMessage {
    return new ZLinkMessage(undefined, message, registry);
  }

  decode<T>(type?: Type<T>): T {
    if (this.encoded === undefined) {
      return this.value as T;
    }
    if (this.encoded.data().length === 0) {
      return undefined as T;
    }
    const serializer = selectDefaultSerializer(this.registry);
    if (serializer !== undefined) {
      return serializer.deserialize(this.encoded, (type ?? Object) as Type<T>);
    }
    const text = this.encoded.getString('utf8');
    try {
      return JSON.parse(text) as T;
    } catch {
      return text as T;
    }
  }

  toMessage(registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>): Message {
    if (this.encoded !== undefined) {
      return this.encoded;
    }
    const serializer = selectDefaultSerializer(registry ?? this.registry);
    if (serializer !== undefined) {
      return serializer.serialize(this.value);
    }
    if (Buffer.isBuffer(this.value) || this.value instanceof Uint8Array) {
      return BindingMessage.from(this.value);
    }
    return BindingMessage.from(Buffer.from(JSON.stringify(this.value ?? null)));
  }

  isEncoded(): boolean {
    return this.encoded !== undefined;
  }
}

export function isZLinkMessage(value: unknown): value is ZLinkMessage {
  return value instanceof ZLinkMessage;
}

export function selectDefaultSerializer(
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): ZLinkMessageSerializer | undefined {
  const serializers = serializerMapOf(registry);
  if (serializers === undefined || serializers.size === 0) {
    return undefined;
  }
  if (serializers.size === 1) {
    return serializers.values().next().value;
  }
  throw new Error('Payload serializer is ambiguous because more than one serializer is registered.');
}

function serializerMapOf(
  registry: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer> | undefined
): ReadonlyMap<string, ZLinkMessageSerializer> | undefined {
  if (registry === undefined) {
    return undefined;
  }
  return 'serializers' in registry ? registry.serializers : registry;
}
