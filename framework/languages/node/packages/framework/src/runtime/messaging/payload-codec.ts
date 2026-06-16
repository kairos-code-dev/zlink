import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { Message, Type, ZLinkMessageSerializer } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';

export interface ZLinkSerializerRegistryLike {
  readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export function encodeFrameworkPayloadMessage(
  payload: unknown,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): Message {
  if (isMessage(payload)) {
    return payload;
  }
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    return BindingMessage.from(payload);
  }

  const serializer = selectDefaultSerializer(registry);
  if (serializer !== undefined) {
    return serializer.serialize(payload);
  }

  return BindingMessage.from(Buffer.from(JSON.stringify(payload ?? null)));
}

export function decodeFrameworkPayloadMessage<T>(
  message: Message,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>,
  type?: Type<T>
): T {
  if (message.data().length === 0) {
    return undefined as T;
  }

  const serializer = selectDefaultSerializer(registry);
  if (serializer !== undefined) {
    return serializer.deserialize(message, (type ?? Object) as Type<T>);
  }

  const text = message.getString('utf8');
  try {
    return JSON.parse(text) as T;
  } catch {
    return text as T;
  }
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
  throw new ZLinkConfigurationException(
    'Payload serializer is ambiguous because more than one serializer is registered.'
  );
}

function serializerMapOf(
  registry: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer> | undefined
): ReadonlyMap<string, ZLinkMessageSerializer> | undefined {
  if (registry === undefined) {
    return undefined;
  }
  return 'serializers' in registry ? registry.serializers : registry;
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}
