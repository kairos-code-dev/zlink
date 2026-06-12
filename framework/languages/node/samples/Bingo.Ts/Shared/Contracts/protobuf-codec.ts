const connector = require('../../../../../packages/stream-connector/dist');
const { Message } = require('@zlink-systems/zlink');

type ZLinkMessage = {
  data(): Buffer;
  close(): void;
};

type EncodedPayload = {
  codec: number;
  payload: Uint8Array;
  messageType?: Function;
};

type FieldSchema = {
  name: string;
  field: number;
  kind: 'string' | 'bool' | 'int32' | 'uint64' | 'bytes' | 'message';
  repeated?: boolean;
  optional?: boolean;
  message?: string;
};

type Envelope = {
  type: string;
  payload: Uint8Array;
};

const protobufCodec = connector.ZlinkStreamCodec.Protobuf;

const messageSchemas: Record<string, FieldSchema[]> = {
  BingoPayloadEnvelope: [
    { name: 'type', field: 1, kind: 'string' },
    { name: 'payload', field: 2, kind: 'bytes' }
  ],
  AuthenticateReq: [
    { name: 'accessToken', field: 1, kind: 'string' }
  ],
  AuthenticateSessionRes: [
    { name: 'actorId', field: 1, kind: 'string' },
    { name: 'displayName', field: 2, kind: 'string' }
  ],
  AuthenticatePlayerRes: [
    { name: 'accepted', field: 1, kind: 'bool' },
    { name: 'actorId', field: 2, kind: 'string', optional: true },
    { name: 'displayName', field: 3, kind: 'string', optional: true },
    { name: 'reason', field: 4, kind: 'string', optional: true }
  ],
  MatchBingoReq: [
    { name: 'mode', field: 1, kind: 'string', optional: true }
  ],
  MatchBingoRes: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'state', field: 2, kind: 'message', message: 'BingoRoomState' }
  ],
  MatchBingoApiRes: [
    { name: 'roomId', field: 1, kind: 'string' }
  ],
  AllocateBingoRoomRes: [
    { name: 'roomId', field: 1, kind: 'string' }
  ],
  EnsurePlayerActorRes: [
    { name: 'actorId', field: 1, kind: 'string' },
    { name: 'actorType', field: 2, kind: 'string' },
    { name: 'actor', field: 3, kind: 'message', message: 'ActorRefSnapshot' }
  ],
  ActorRefSnapshot: [
    { name: 'nodeRid', field: 1, kind: 'bytes' },
    { name: 'actorId', field: 2, kind: 'string' },
    { name: 'generation', field: 3, kind: 'uint64' }
  ],
  BingoRoomJoinReq: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'actorId', field: 2, kind: 'string' },
    { name: 'displayName', field: 3, kind: 'string' }
  ],
  StateEnvelope: [
    { name: 'state', field: 1, kind: 'message', message: 'BingoRoomState' }
  ],
  RoomJoinError: [
    { name: 'error', field: 1, kind: 'string' }
  ],
  SubmitBingoCardReq: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'card', field: 2, kind: 'int32', repeated: true }
  ],
  SubmitBingoCardRes: [
    { name: 'state', field: 1, kind: 'message', message: 'BingoRoomState' }
  ],
  PlayerJoinedNotify: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'actorId', field: 2, kind: 'string' },
    { name: 'displayName', field: 3, kind: 'string' },
    { name: 'seat', field: 4, kind: 'int32' },
    { name: 'isHost', field: 5, kind: 'bool' },
    { name: 'state', field: 6, kind: 'message', message: 'BingoRoomState' }
  ],
  BingoNumberDrawnNotify: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'drawSeq', field: 2, kind: 'int32' },
    { name: 'number', field: 3, kind: 'int32' },
    { name: 'state', field: 4, kind: 'message', message: 'BingoRoomState' }
  ],
  BingoRoomState: [
    { name: 'roomId', field: 1, kind: 'string' },
    { name: 'status', field: 2, kind: 'string' },
    { name: 'hostActorId', field: 3, kind: 'string', optional: true },
    { name: 'canStart', field: 4, kind: 'bool' },
    { name: 'drawSeq', field: 5, kind: 'int32' },
    { name: 'lastDrawnNumber', field: 6, kind: 'int32', optional: true },
    { name: 'drawnNumbers', field: 7, kind: 'int32', repeated: true },
    { name: 'players', field: 8, kind: 'message', message: 'BingoPlayerState', repeated: true },
    { name: 'winners', field: 9, kind: 'string', repeated: true }
  ],
  BingoPlayerState: [
    { name: 'actorId', field: 1, kind: 'string' },
    { name: 'displayName', field: 2, kind: 'string' },
    { name: 'seat', field: 3, kind: 'int32' },
    { name: 'isHost', field: 4, kind: 'bool' },
    { name: 'card', field: 5, kind: 'int32', repeated: true },
    { name: 'marks', field: 6, kind: 'bool', repeated: true },
    { name: 'completedLines', field: 7, kind: 'int32' }
  ]
};

const messageTypesByPacketName: Record<string, string> = {
  AuthenticateReq: 'AuthenticateReq',
  AuthenticatePlayerReq: 'AuthenticateReq',
  MatchBingoReq: 'MatchBingoReq',
  MatchBingoRes: 'MatchBingoRes',
  MatchBingoApiRes: 'MatchBingoApiRes',
  AllocateBingoRoomRes: 'AllocateBingoRoomRes',
  SubmitBingoCardReq: 'SubmitBingoCardReq',
  SubmitBingoCardRes: 'SubmitBingoCardRes',
  PlayerJoinedNotify: 'PlayerJoinedNotify',
  BingoGameStartedNotify: 'StateEnvelope',
  BingoNumberDrawnNotify: 'BingoNumberDrawnNotify',
  BingoGameEndedNotify: 'StateEnvelope'
};

const responseTypesByRequestPacketName: Record<string, string> = {
  AuthenticateReq: 'AuthenticateSessionRes',
  MatchBingoReq: 'MatchBingoRes',
  SubmitBingoCardReq: 'SubmitBingoCardRes'
};

const bingoProtobufCodec = {
  encode(payload: unknown, messageType?: Function): EncodedPayload {
    return toBingoProto(payload, messageType);
  },

  decode<TPayload = unknown>(payload: EncodedPayload): TPayload {
    return fromBingoProto(payload) as TPayload;
  }
};

function toBingoProto(value: unknown, messageType?: Function, packetName?: string): EncodedPayload {
  const type = inferMessageName(value, messageType, packetName);
  return {
    codec: protobufCodec,
    payload: encodeEnvelope({ type, payload: encodeMessage(type, value) }),
    messageType: messageType ?? inferMessageType(value)
  };
}

function fromBingoProto<TValue = unknown>(payload: EncodedPayload): TValue {
  if (payload.codec !== protobufCodec) {
    throw new Error(`Stream payload codec is ${payload.codec}, not Protobuf.`);
  }
  const envelope = decodeEnvelope(payload.payload);
  return decodeMessage(envelope.type, envelope.payload) as TValue;
}

function createProtobufMessage(value: unknown): ZLinkMessage {
  const type = inferMessageName(value);
  return Message.from(Buffer.from(encodeEnvelope({ type, payload: encodeMessage(type, value) })));
}

function readProtobufMessage<TValue = unknown>(message: ZLinkMessage): TValue {
  const envelope = decodeEnvelope(message.data());
  return decodeMessage(envelope.type, envelope.payload) as TValue;
}

function encodeEnvelope(envelope: Envelope): Uint8Array {
  return encodeMessage('BingoPayloadEnvelope', envelope);
}

function decodeEnvelope(bytes: Uint8Array): Envelope {
  return decodeMessage('BingoPayloadEnvelope', bytes) as Envelope;
}

function encodeMessage(type: string, value: unknown): Uint8Array {
  const schema = requireSchema(type);
  const chunks: Uint8Array[] = [];
  const record = isRecord(value) ? value : {};
  for (const field of schema) {
    const rawValue = record[field.name];
    if (rawValue === undefined || rawValue === null) {
      continue;
    }
    const values = field.repeated ? rawValue as unknown[] : [rawValue];
    for (const entry of values) {
      chunks.push(encodeField(field, entry));
    }
  }
  return concat(chunks);
}

function decodeMessage(type: string, bytes: Uint8Array): Record<string, unknown> {
  const schema = requireSchema(type);
  const byNumber = new Map(schema.map((field) => [field.field, field]));
  const output: Record<string, unknown> = {};
  let offset = 0;
  while (offset < bytes.length) {
    const tag = decodeVarint(bytes, offset);
    offset = tag.offset;
    const fieldNumber = tag.value >> 3;
    const wireType = tag.value & 0x07;
    const field = byNumber.get(fieldNumber);
    const decoded = decodeFieldValue(bytes, offset, wireType, field);
    offset = decoded.offset;
    if (field === undefined) {
      continue;
    }
    if (field.repeated) {
      const current = output[field.name] as unknown[] | undefined;
      output[field.name] = [...(current ?? []), decoded.value];
    } else {
      output[field.name] = decoded.value;
    }
  }
  for (const field of schema) {
    if (output[field.name] !== undefined) {
      continue;
    }
    if (field.repeated) {
      output[field.name] = [];
    } else if (field.optional) {
      output[field.name] = null;
    } else if (field.kind === 'bool') {
      output[field.name] = false;
    } else if (field.kind === 'int32' || field.kind === 'uint64') {
      output[field.name] = 0;
    } else if (field.kind === 'bytes') {
      output[field.name] = Uint8Array.from([]);
    } else if (field.kind === 'string') {
      output[field.name] = '';
    }
  }
  return output;
}

function encodeField(field: FieldSchema, value: unknown): Uint8Array {
  if (field.kind === 'bool' || field.kind === 'int32' || field.kind === 'uint64') {
    return concat([
      encodeVarint((field.field << 3) | 0),
      encodeVarint(field.kind === 'bool' ? (value ? 1 : 0) : Number(value))
    ]);
  }
  const payload = encodeLengthDelimitedValue(field, value);
  return concat([
    encodeVarint((field.field << 3) | 2),
    encodeVarint(payload.length),
    payload
  ]);
}

function encodeLengthDelimitedValue(field: FieldSchema, value: unknown): Uint8Array {
  if (field.kind === 'string') {
    return Buffer.from(String(value), 'utf8');
  }
  if (field.kind === 'bytes') {
    if (typeof value === 'string') {
      return Buffer.from(value, 'utf8');
    }
    if (value instanceof Uint8Array) {
      return Uint8Array.from(value);
    }
    throw new Error(`Field '${field.name}' expects bytes.`);
  }
  if (field.kind === 'message') {
    return encodeMessage(field.message ?? '', value);
  }
  throw new Error(`Field '${field.name}' cannot use length-delimited protobuf encoding.`);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function decodeFieldValue(
  bytes: Uint8Array,
  offset: number,
  wireType: number,
  field: FieldSchema | undefined
): { value: unknown; offset: number } {
  if (wireType === 0) {
    const decoded = decodeVarint(bytes, offset);
    if (field?.kind === 'bool') {
      return { value: decoded.value !== 0, offset: decoded.offset };
    }
    return { value: decoded.value, offset: decoded.offset };
  }
  if (wireType === 2) {
    const length = decodeVarint(bytes, offset);
    const start = length.offset;
    const end = start + length.value;
    const payload = bytes.subarray(start, end);
    if (field?.kind === 'string') {
      return { value: Buffer.from(payload).toString('utf8'), offset: end };
    }
    if (field?.kind === 'bytes') {
      return { value: Uint8Array.from(payload), offset: end };
    }
    if (field?.kind === 'message') {
      return { value: decodeMessage(field.message ?? '', payload), offset: end };
    }
    return { value: payload, offset: end };
  }
  throw new Error(`Unsupported protobuf wire type ${wireType}.`);
}

function requireSchema(type: string): FieldSchema[] {
  const schema = messageSchemas[type];
  if (schema === undefined) {
    throw new Error(`Bingo Protobuf message '${type}' is not defined.`);
  }
  return schema;
}

function inferMessageName(value: unknown, messageType?: Function, packetName?: string): string {
  if (packetName !== undefined && responseTypesByRequestPacketName[packetName] !== undefined) {
    return responseTypesByRequestPacketName[packetName];
  }
  if (packetName !== undefined && messageTypesByPacketName[packetName] !== undefined) {
    return messageTypesByPacketName[packetName];
  }
  if (messageType?.name !== undefined && messageSchemas[messageType.name] !== undefined) {
    return messageType.name;
  }
  const constructor = inferMessageType(value);
  if (constructor?.name !== undefined && messageSchemas[constructor.name] !== undefined) {
    return constructor.name;
  }
  const payload = value as Record<string, unknown>;
  if (payload === null || payload === undefined) {
    throw new Error('Cannot infer Bingo Protobuf message type for empty payload.');
  }
  if (typeof payload.error === 'string') {
    return 'RoomJoinError';
  }
  if ('drawSeq' in payload && 'number' in payload) {
    return 'BingoNumberDrawnNotify';
  }
  if ('seat' in payload && 'isHost' in payload) {
    return 'PlayerJoinedNotify';
  }
  if (Array.isArray(payload.card) && typeof payload.roomId === 'string') {
    return 'SubmitBingoCardReq';
  }
  if ('state' in payload) {
    return 'StateEnvelope';
  }
  if (typeof payload.accepted === 'boolean') {
    return 'AuthenticatePlayerRes';
  }
  if (typeof payload.actorType === 'string') {
    return 'EnsurePlayerActorRes';
  }
  if (typeof payload.accessToken === 'string') {
    return 'AuthenticateReq';
  }
  if ('mode' in payload) {
    return 'MatchBingoReq';
  }
  if (typeof payload.actorId === 'string' && typeof payload.displayName === 'string') {
    return 'AuthenticateSessionRes';
  }
  if (typeof payload.roomId === 'string') {
    return 'MatchBingoApiRes';
  }
  throw new Error(`Cannot infer Bingo Protobuf message type for payload keys: ${Object.keys(payload).join(', ')}`);
}

function inferMessageType(value: unknown): Function | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const constructor = Object.getPrototypeOf(value)?.constructor;
  return constructor === Object ? undefined : constructor;
}

function encodeVarint(value: number): Uint8Array {
  const bytes: number[] = [];
  let remaining = value;
  do {
    let current = remaining & 0x7f;
    remaining = Math.floor(remaining / 128);
    if (remaining > 0) {
      current |= 0x80;
    }
    bytes.push(current);
  } while (remaining > 0);
  return Uint8Array.from(bytes);
}

function decodeVarint(bytes: Uint8Array, start: number): { value: number; offset: number } {
  let value = 0;
  let shift = 0;
  let offset = start;
  while (offset < bytes.length) {
    const current = bytes[offset++];
    value += (current & 0x7f) * 2 ** shift;
    if ((current & 0x80) === 0) {
      return { value, offset };
    }
    shift += 7;
  }
  throw new Error('Bingo Protobuf varint is incomplete.');
}

function concat(chunks: Uint8Array[]): Uint8Array {
  const length = chunks.reduce((total, chunk) => total + chunk.length, 0);
  const output = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) {
    output.set(chunk, offset);
    offset += chunk.length;
  }
  return output;
}

export {
  bingoProtobufCodec,
  createProtobufMessage,
  fromBingoProto,
  readProtobufMessage,
  toBingoProto
};
