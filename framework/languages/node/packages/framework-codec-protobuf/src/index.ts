import { createRequire } from 'node:module';
import path from 'node:path';
import {
  ZLinkEncodedPayload,
  type ZLinkCodecExtension,
  type ZLinkCodecRegistryBuilder,
  type ZLinkMessageSerializer
} from '@zlink-systems/framework';
import {
  ZlinkStreamCodec,
  type ZlinkStreamEncodedPayload,
  type ZlinkStreamPayloadCodec
} from '@zlink-systems/stream-connector';

export const ZLINK_PROTOBUF_CONTENT_TYPE = 'application/x-protobuf';
export const zlinkStreamProtobufCodecName = 'protobuf';

export interface ProtobufType<T> {
  encode(message: T): { finish(): Uint8Array };
  decode(reader: Uint8Array): T;
}

export interface ProtobufEnvelopeCodecOptions {
  encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload;
  decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload;
}

export interface ProtobufJsEnvelopeCodecOptions {
  protoPath: string;
  packageName: string;
  envelopeType: string;
  messageTypesByPacketName?: Readonly<Record<string, string>>;
  responseTypesByRequestPacketName?: Readonly<Record<string, string>>;
  inferredTypesByConstructorName?: ReadonlySet<string>;
  inferMessageName?: (
    value: unknown,
    context: {
      messageType?: Function;
      packetName?: string;
      mappedType?: string;
      responseType?: string;
      hasType(typeName: string): boolean;
    }
  ) => string | undefined;
}

interface ProtobufJsRoot {
  lookupType(name: string): ProtobufJsType;
}

interface ProtobufJsType {
  fromObject(value: unknown): unknown;
  toObject(message: unknown, options?: ProtobufConversionOptions): Record<string, unknown>;
  encode(message: unknown): { finish(): Uint8Array };
  decode(bytes: Uint8Array): unknown;
}

interface ProtobufConversionOptions {
  defaults?: boolean;
  arrays?: boolean;
  longs?: NumberConstructor | StringConstructor;
  bytes?: typeof Buffer | StringConstructor;
}

interface ProtobufJs {
  loadSync(filename: string): ProtobufJsRoot;
}

const enum WireType {
  Varint = 0,
  Fixed64 = 1,
  LengthDelimited = 2
}

const enum ValueKind {
  Null = 0,
  Bool = 1,
  Number = 2,
  String = 3,
  Object = 4,
  Array = 5
}

export type ZLinkProtobufCodecExtension = ZLinkCodecExtension & ZlinkStreamPayloadCodec;

export type ZLinkProtobufEnvelopeCodecExtension = ZLinkProtobufCodecExtension & {
  encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload;
};

export function zlinkProtobufCodec(): ZLinkProtobufCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistryBuilder): void {
      codecs.addSerializer(ZLINK_PROTOBUF_CONTENT_TYPE, createProtobufMessageSerializer());
      codecs.addStreamCodec(ZLINK_PROTOBUF_CONTENT_TYPE, this);
    },

    encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
      return toProto(payload, createValueProtobufType(), messageType);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return fromProto(payload, createValueProtobufType()) as unknown as TPayload;
    }
  };
}

export function createProtobufMessageSerializer(): ZLinkMessageSerializer {
  return {
    serialize<T>(value: T): ZLinkEncodedPayload {
      return ZLinkEncodedPayload.from(encodeValue(value));
    },

    deserialize<T>(payload: ZLinkEncodedPayload): T {
      return decodeValue(Buffer.from(payload.data())) as T;
    }
  };
}

function createValueProtobufType(): ProtobufType<unknown> {
  return {
    encode(value: unknown): { finish(): Uint8Array } {
      return {
        finish(): Uint8Array {
          return encodeValue(value);
        }
      };
    },

    decode(reader: Uint8Array): unknown {
      return decodeValue(Buffer.from(reader));
    }
  };
}

export function createZlinkStreamProtobufCodec<T>(type: ProtobufType<T>): ZlinkStreamPayloadCodec {
  return {
    encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
      return toProto(payload as T, type, messageType);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return fromProto(payload, type) as unknown as TPayload;
    }
  };
}

export function createZlinkStreamProtobufEnvelopeCodec(
  options: ProtobufEnvelopeCodecOptions
): ZLinkProtobufEnvelopeCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistryBuilder): void {
      codecs.addSerializer(ZLINK_PROTOBUF_CONTENT_TYPE, createProtobufMessageSerializer());
      codecs.addStreamCodec(ZLINK_PROTOBUF_CONTENT_TYPE, this);
    },

    encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload {
      return options.encode(payload, messageType, packetName);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return options.decode<TPayload>(payload);
    }
  };
}

export function createZlinkProtobufJsEnvelopeCodec(
  options: ProtobufJsEnvelopeCodecOptions
): ZLinkProtobufEnvelopeCodecExtension {
  const protobuf = loadProtobufJs() as ProtobufJs;
  const root = protobuf.loadSync(options.protoPath);
  const lookupType = (typeName: string): ProtobufJsType =>
    root.lookupType(`${options.packageName}.${typeName}`);
  const hasType = (typeName: string): boolean => {
    try {
      lookupType(typeName);
      return true;
    } catch {
      return false;
    }
  };
  const encodeMessage = (typeName: string, value: unknown): Uint8Array => {
    const type = lookupType(typeName);
    return type.encode(type.fromObject(value)).finish();
  };
  const decodeMessage = (typeName: string, bytes: Uint8Array): Record<string, unknown> => {
    const type = lookupType(typeName);
    return type.toObject(type.decode(bytes), {
      defaults: true,
      arrays: true,
      longs: Number,
      bytes: Buffer
    });
  };
  const resolveMessageName = (
    value: unknown,
    messageType?: Function,
    packetName?: string,
    preferResponseType = false
  ): string => {
    const responseType = packetName === undefined
      ? undefined
      : options.responseTypesByRequestPacketName?.[packetName];
    if (preferResponseType && responseType !== undefined) {
      return responseType;
    }
    const mappedType = packetName === undefined
      ? undefined
      : options.messageTypesByPacketName?.[packetName];
    if (mappedType !== undefined) {
      return mappedType;
    }
    if (messageType?.name !== undefined && hasType(messageType.name)) {
      return messageType.name;
    }
    const constructor = inferMessageType(value);
    if (
      constructor?.name !== undefined &&
      options.inferredTypesByConstructorName?.has(constructor.name) === true
    ) {
      return constructor.name;
    }
    const inferred = options.inferMessageName?.(value, {
      messageType,
      packetName,
      mappedType,
      responseType,
      hasType
    });
    if (inferred !== undefined) {
      return inferred;
    }
    if (value === null || value === undefined) {
      throw new Error('Cannot infer Protobuf message type for empty payload.');
    }
    throw new Error(
      `Cannot infer Protobuf message type for payload keys: ${Object.keys(value as Record<string, unknown>).join(', ')}`
    );
  };
  return createZlinkStreamProtobufEnvelopeCodec({
    encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload {
      const type = resolveMessageName(payload, messageType, packetName, true);
      return {
        codec: ZlinkStreamCodec.Protobuf,
        payload: encodeMessage(options.envelopeType, { type, payload: encodeMessage(type, payload) }),
        messageType: messageType ?? inferMessageType(payload)
      };
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      ensureProtobuf(payload);
      const envelope = decodeMessage(options.envelopeType, payload.payload) as { type: string; payload: Uint8Array };
      return decodeMessage(envelope.type, envelope.payload) as TPayload;
    }
  });
}

export function toProto<T>(value: T, type: ProtobufType<T>, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: ZlinkStreamCodec.Protobuf,
    payload: type.encode(value).finish(),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromProto<T>(payload: ZlinkStreamEncodedPayload, type: ProtobufType<T>): T {
  ensureProtobuf(payload);
  return type.decode(payload.payload);
}

export function loadProtobufJs(): unknown {
  const requireProtobuf = createRequire(__filename);
  try {
    return requireProtobuf('protobufjs') as unknown;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    const zlinkEntryPath = requireProtobuf.resolve('@zlink-systems/zlink');
    return requireProtobuf(path.resolve(path.dirname(zlinkEntryPath), '..', 'node_modules', 'protobufjs')) as unknown;
  }
}

function encodeValue(value: unknown): Buffer {
  if (value === null || value === undefined) {
    return encodeFields([encodeVarintField(1, ValueKind.Null)]);
  }
  if (typeof value === 'boolean') {
    return encodeFields([
      encodeVarintField(1, ValueKind.Bool),
      encodeVarintField(2, value ? 1 : 0)
    ]);
  }
  if (typeof value === 'number') {
    return encodeFields([
      encodeVarintField(1, ValueKind.Number),
      encodeDoubleField(3, value)
    ]);
  }
  if (typeof value === 'string') {
    return encodeFields([
      encodeVarintField(1, ValueKind.String),
      encodeBytesField(4, Buffer.from(value))
    ]);
  }
  if (Array.isArray(value)) {
    return encodeFields([
      encodeVarintField(1, ValueKind.Array),
      ...value.map((item) => encodeBytesField(6, encodeValue(item)))
    ]);
  }
  if (typeof value === 'object') {
    return encodeFields([
      encodeVarintField(1, ValueKind.Object),
      ...Object.entries(value as Record<string, unknown>)
        .map(([key, entryValue]) => encodeBytesField(5, encodeObjectEntry(key, entryValue)))
    ]);
  }
  throw new Error(`Protobuf serializer cannot encode value of type '${typeof value}'.`);
}

function decodeValue(bytes: Buffer): unknown {
  let kind = ValueKind.Null;
  let boolValue = false;
  let numberValue = 0;
  let stringValue = '';
  const objectValue: Record<string, unknown> = {};
  const arrayValue: unknown[] = [];

  for (const field of readFields(bytes)) {
    switch (field.fieldNumber) {
      case 1:
        kind = Number(readVarintPayload(field)) as ValueKind;
        break;
      case 2:
        boolValue = readVarintPayload(field) !== 0n;
        break;
      case 3:
        numberValue = readDoublePayload(field);
        break;
      case 4:
        stringValue = readBytesPayload(field).toString();
        break;
      case 5: {
        const entry = decodeObjectEntry(readBytesPayload(field));
        objectValue[entry.key] = entry.value;
        break;
      }
      case 6:
        arrayValue.push(decodeValue(readBytesPayload(field)));
        break;
      default:
        break;
    }
  }

  switch (kind) {
    case ValueKind.Null:
      return null;
    case ValueKind.Bool:
      return boolValue;
    case ValueKind.Number:
      return numberValue;
    case ValueKind.String:
      return stringValue;
    case ValueKind.Object:
      return objectValue;
    case ValueKind.Array:
      return arrayValue;
    default:
      throw new Error(`Protobuf serializer cannot decode value kind '${kind}'.`);
  }
}

function encodeObjectEntry(key: string, value: unknown): Buffer {
  return encodeFields([
    encodeBytesField(1, Buffer.from(key)),
    encodeBytesField(2, encodeValue(value))
  ]);
}

function decodeObjectEntry(bytes: Buffer): { readonly key: string; readonly value: unknown } {
  let key = '';
  let value: unknown = null;
  for (const field of readFields(bytes)) {
    if (field.fieldNumber === 1) {
      key = readBytesPayload(field).toString();
    } else if (field.fieldNumber === 2) {
      value = decodeValue(readBytesPayload(field));
    }
  }
  return { key, value };
}

function ensureProtobuf(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== ZlinkStreamCodec.Protobuf) {
    throw new Error(`Stream payload codec is ${payload.codec}, not Protobuf.`);
  }
}

function inferMessageType(value: unknown): Function | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const constructor = Object.getPrototypeOf(value)?.constructor;
  return constructor === Object ? undefined : constructor;
}

function encodeFields(fields: readonly Buffer[]): Buffer {
  return Buffer.concat(fields);
}

function encodeVarintField(fieldNumber: number, value: number | bigint): Buffer {
  return Buffer.concat([
    encodeVarint(fieldKey(fieldNumber, WireType.Varint)),
    encodeVarint(value)
  ]);
}

function encodeDoubleField(fieldNumber: number, value: number): Buffer {
  const payload = Buffer.allocUnsafe(8);
  payload.writeDoubleLE(value);
  return Buffer.concat([
    encodeVarint(fieldKey(fieldNumber, WireType.Fixed64)),
    payload
  ]);
}

function encodeBytesField(fieldNumber: number, value: Buffer): Buffer {
  return Buffer.concat([
    encodeVarint(fieldKey(fieldNumber, WireType.LengthDelimited)),
    encodeVarint(value.length),
    value
  ]);
}

function fieldKey(fieldNumber: number, wireType: WireType): number {
  return (fieldNumber << 3) | wireType;
}

function encodeVarint(value: number | bigint): Buffer {
  let remaining = BigInt(value);
  const bytes = Buffer.allocUnsafe(10);
  let offset = 0;
  do {
    let byte = Number(remaining & 0x7fn);
    remaining >>= 7n;
    if (remaining !== 0n) {
      byte |= 0x80;
    }
    bytes[offset] = byte;
    offset += 1;
  } while (remaining !== 0n);
  return Buffer.from(bytes.subarray(0, offset));
}

type WireField = {
  readonly fieldNumber: number;
  readonly wireType: number;
  readonly payload: Buffer;
};

function readFields(bytes: Buffer): WireField[] {
  const fields: WireField[] = [];
  let offset = 0;
  while (offset < bytes.length) {
    const key = readVarint(bytes, offset);
    offset = key.offset;
    const fieldNumber = Number(key.value >> 3n);
    const wireType = Number(key.value & 0x07n);
    if (wireType === WireType.Varint) {
      const value = readVarint(bytes, offset);
      fields.push({ fieldNumber, wireType, payload: bytes.subarray(offset, value.offset) });
      offset = value.offset;
    } else if (wireType === WireType.Fixed64) {
      fields.push({ fieldNumber, wireType, payload: bytes.subarray(offset, offset + 8) });
      offset += 8;
    } else if (wireType === WireType.LengthDelimited) {
      const length = readVarint(bytes, offset);
      offset = length.offset;
      const end = offset + Number(length.value);
      fields.push({ fieldNumber, wireType, payload: bytes.subarray(offset, end) });
      offset = end;
    } else {
      throw new Error(`Protobuf serializer cannot read wire type '${wireType}'.`);
    }
  }
  return fields;
}

function readVarintPayload(field: WireField): bigint {
  ensureWireType(field, WireType.Varint);
  return readVarint(field.payload, 0).value;
}

function readDoublePayload(field: WireField): number {
  ensureWireType(field, WireType.Fixed64);
  return field.payload.readDoubleLE();
}

function readBytesPayload(field: WireField): Buffer {
  ensureWireType(field, WireType.LengthDelimited);
  return field.payload;
}

function ensureWireType(field: WireField, expected: WireType): void {
  if (field.wireType !== expected) {
    throw new Error(`Protobuf field '${field.fieldNumber}' has wire type '${field.wireType}', not '${expected}'.`);
  }
}

function readVarint(bytes: Buffer, start: number): { readonly value: bigint; readonly offset: number } {
  let value = 0n;
  let shift = 0n;
  let offset = start;
  while (offset < bytes.length) {
    const byte = BigInt(bytes[offset]);
    value |= (byte & 0x7fn) << shift;
    offset += 1;
    if ((byte & 0x80n) === 0n) {
      return { value, offset };
    }
    shift += 7n;
  }
  throw new Error('Protobuf varint is truncated.');
}
