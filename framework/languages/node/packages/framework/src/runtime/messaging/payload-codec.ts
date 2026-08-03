import { ZLinkFrameworkInternalErrorKind, createInternalFrameworkException  } from '../framework-errors-internal';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import {
  isZLinkMessage,
  ZLinkMessage,
  type Type,
  ZLinkEncodedPayload,
  type ZLinkMessageSerializer
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { createZLinkMessageFromEncoded } from '../../contracts/Common/ZLinkMessage';
import { ZLinkConfigurationException } from '../configuration';

export interface ZLinkSerializerRegistryLike {
  readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export function encodeFrameworkPayloadMessage(
  payload: unknown,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): Message {
  if (isZLinkMessage(payload)) {
    if (payload.isEncoded()) {
      return toBindingMessage(payload.toEncodedPayload());
    }
    return encodeFrameworkPayloadMessage(payload.decode(), registry);
  }
  if (isMessage(payload)) {
    throw new ZLinkConfigurationException(
      'Raw binding Message is not accepted by the default framework payload API. Use ZLinkMessage.fromEncoded(...) for explicit raw forwarding.'
    );
  }
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    throw new ZLinkConfigurationException(
      'Raw Buffer payload is not accepted by the default framework payload API. Wrap a DTO with ZLinkMessage.from(...) or use ZLinkMessage.fromEncoded(...) for explicit raw forwarding.'
    );
  }

  const serializer = selectSerializer(payload, registry);
  if (serializer !== undefined) {
    return toBindingMessage(serializer.serialize(payload));
  }

  return BindingMessage.from(Buffer.from(JSON.stringify(payload ?? null)));
}

export function decodeFrameworkPayloadMessage<T>(
  message: Message,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>,
  type?: Type<T>
): T {
  return decodeFrameworkPayload(message, registry, type, false);
}

export function decodeFrameworkTypedPayloadMessage<T>(
  message: Message,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>,
  type?: Type<T>
): T {
  return decodeFrameworkPayload(message, registry, type, true);
}

function decodeFrameworkPayload<T>(
  message: Message,
  registry: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer> | undefined,
  type: Type<T> | undefined,
  rejectInvalidJson: boolean
): T {
  if (message.data().length === 0) {
    return undefined as T;
  }

  const serializer = selectDefaultSerializer(registry);
  const parsedPayload = parseJsonPayload(message);
  if (serializer !== undefined) {
    // A selective extension is also the only available decoder for an
    // inbound non-JSON stream frame. JSON remains the wire fallback when the
    // same registry is used for framework-owned messages that were encoded
    // without a matching application type.
    if (parsedPayload.valid && isSelectableSerializer(serializer)) {
      return parsedPayload.value as T;
    }
    return serializer.deserialize(
      ZLinkEncodedPayload.from(message.data()),
      (type ?? Object) as Type<T>
    );
  }

  if (parsedPayload.valid) {
    return parsedPayload.value as T;
  }
  const text = message.getString('utf8');
  if (!rejectInvalidJson) {
    return text as T;
  }
  throw createInternalFrameworkException(
    ZLinkFrameworkInternalErrorKind.PayloadDecodeFailed,
    'PayloadDecodeFailed: framework payload is not valid JSON.',
    false,
    parsedPayload.error
  );
}

export function wrapFrameworkPayloadMessage(
  message: Message,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): ZLinkMessage {
  const payload = ZLinkEncodedPayload.from(message.data());
  return createZLinkMessageFromEncoded(payload, <T>(type?: Type<T>) =>
    decodeFrameworkPayload(message, registry, type, false));
}

export function selectDefaultSerializer(
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): ZLinkMessageSerializer | undefined {
  const serializers = serializerMapOf(registry);
  if (serializers?.size === 1) {
    const only = serializers.values().next().value as ZLinkSelectableMessageSerializer | undefined;
    // Decode has no object value with which to run canSerialize. A single
    // selective extension is therefore the only possible decoder for its
    // non-JSON stream payloads; encode-side selection still uses the actual
    // payload and falls back to JSON when the extension does not match.
    if (only?.canSerialize !== undefined) {
      return only;
    }
  }
  return selectSerializer(undefined, registry);
}

export function selectSerializer(
  value: unknown,
  registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>
): ZLinkMessageSerializer | undefined {
  const serializers = serializerMapOf(registry);
  if (serializers === undefined || serializers.size === 0) {
    return undefined;
  }
  const selectable = [...serializers.values()] as ZLinkSelectableMessageSerializer[];
  const matching = selectable.filter((serializer) =>
    serializer.canSerialize?.(value) === true
  );
  if (matching.length === 1) {
    return matching[0];
  }
  if (matching.length > 1) {
    throw new ZLinkConfigurationException(
      'Payload serializer is ambiguous because more than one serializer accepts the payload.'
    );
  }
  if (serializers.size === 1) {
    const only = selectable[0];
    // A selective extension must not become the process-wide default for
    // framework-owned payloads that it explicitly does not accept. JSON is
    // the typed payload fallback when no extension matches.
    return only.canSerialize === undefined || only.canSerialize(value) ? only : undefined;
  }
  if (selectable.every((serializer) => serializer.canSerialize !== undefined)) {
    return undefined;
  }
  throw new ZLinkConfigurationException(
    'Payload serializer is ambiguous because more than one serializer is registered.'
  );
}

interface ZLinkSelectableMessageSerializer extends ZLinkMessageSerializer {
  canSerialize?(value: unknown): boolean;
}

function isSelectableSerializer(serializer: ZLinkMessageSerializer): serializer is ZLinkSelectableMessageSerializer & { canSerialize: (value: unknown) => boolean } {
  return typeof (serializer as ZLinkSelectableMessageSerializer).canSerialize === 'function';
}

function parseJsonPayload(message: Message): {
  readonly valid: true;
  readonly value: unknown;
  readonly error?: undefined;
} | {
  readonly valid: false;
  readonly value?: undefined;
  readonly error: unknown;
} {
  try {
    return { valid: true, value: JSON.parse(message.getString('utf8')) };
  } catch (error) {
    return { valid: false, error };
  }
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

function toBindingMessage(payload: ZLinkEncodedPayload): Message {
  return BindingMessage.from(payload.data());
}
