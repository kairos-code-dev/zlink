import * as connector from '@zlink-systems/stream-connector';
import { loadProtobufJs } from '@zlink-systems/stream-connector-protobuf';
import path from 'node:path';

type EncodedPayload = {
  codec: number;
  payload: Uint8Array;
  messageType?: Function;
};

type ProtobufRoot = {
  lookupType(name: string): ProtobufType;
};

type ProtobufType = {
  fromObject(value: unknown): unknown;
  toObject(message: unknown, options?: ProtobufConversionOptions): Record<string, unknown>;
  encode(message: unknown): { finish(): Uint8Array };
  decode(bytes: Uint8Array): unknown;
};

type ProtobufConversionOptions = {
  defaults?: boolean;
  arrays?: boolean;
  longs?: NumberConstructor | StringConstructor;
  bytes?: typeof Buffer | StringConstructor;
};

type ProtobufJs = {
  loadSync(filename: string): ProtobufRoot;
};

const protobufCodec = connector.ZlinkStreamCodec.Protobuf;
const protoPackage = 'zlink.samples.bingo';
const protobuf = loadProtobufJs() as ProtobufJs;
const root = protobuf.loadSync(resolveProtoPath());

const messageTypesByPacketName: Record<string, string> = {
  AuthenticateReq: 'AuthenticateReq',
  AuthenticatePlayerReq: 'AuthenticatePlayerReq',
  MatchBingoReq: 'MatchBingoReq',
  MatchBingoApiReq: 'MatchBingoApiReq',
  MatchBingoRes: 'MatchBingoRes',
  MatchBingoApiRes: 'MatchBingoApiRes',
  AllocateBingoRoomReq: 'AllocateBingoRoomReq',
  AllocateBingoRoomRes: 'AllocateBingoRoomRes',
  EnsurePlayerActorReq: 'EnsurePlayerActorReq',
  BingoNotificationsReq: 'BingoNotificationsReq',
  SubmitBingoCardReq: 'SubmitBingoCardReq',
  SubmitBingoCardRes: 'SubmitBingoCardRes',
  PlayerJoinedNotify: 'PlayerJoinedNotify',
  BingoGameStartedNotify: 'BingoGameStartedNotify',
  BingoNumberDrawnNotify: 'BingoNumberDrawnNotify',
  BingoGameEndedNotify: 'BingoGameEndedNotify'
};

const responseTypesByRequestPacketName: Record<string, string> = {
  AuthenticatePlayerReq: 'AuthenticatePlayerRes',
  AuthenticateReq: 'AuthenticateSessionRes',
  MatchBingoApiReq: 'MatchBingoApiRes',
  AllocateBingoRoomReq: 'AllocateBingoRoomRes',
  EnsurePlayerActorReq: 'EnsurePlayerActorRes',
  MatchBingoReq: 'MatchBingoRes',
  SubmitBingoCardReq: 'SubmitBingoCardRes',
  BingoNotificationsReq: 'BingoNotificationBatch'
};

const inferredTypesByConstructorName = new Set([
  'AuthenticateReq',
  'AuthenticatePlayerReq',
  'MatchBingoReq',
  'AllocateBingoRoomReq',
  'EnsurePlayerActorReq',
  'SubmitBingoCardReq',
  'BingoNotificationsReq'
]);

const bingoProtobufCodec = {
  encode(payload: unknown, messageType?: Function): EncodedPayload {
    return toBingoProto(payload, messageType);
  },

  decode<TPayload = unknown>(payload: EncodedPayload): TPayload {
    return fromBingoProto(payload) as TPayload;
  }
};

function bingoPayloadBase64(value: unknown, packetName?: string): string {
  return Buffer.from(toBingoProto(value, undefined, packetName).payload).toString('base64');
}

function toBingoProto(value: unknown, messageType?: Function, packetName?: string): EncodedPayload {
  const type = inferMessageName(value, messageType, packetName, true);
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

function encodeEnvelope(envelope: { type: string; payload: Uint8Array }): Uint8Array {
  return encodeMessage('BingoPayloadEnvelope', envelope);
}

function decodeEnvelope(bytes: Uint8Array): { type: string; payload: Uint8Array } {
  return decodeMessage('BingoPayloadEnvelope', bytes) as { type: string; payload: Uint8Array };
}

function encodeMessage(typeName: string, value: unknown): Uint8Array {
  const type = lookupType(typeName);
  return type.encode(type.fromObject(value)).finish();
}

function decodeMessage(typeName: string, bytes: Uint8Array): Record<string, unknown> {
  const type = lookupType(typeName);
  return type.toObject(type.decode(bytes), {
    defaults: true,
    arrays: true,
    longs: Number,
    bytes: Buffer
  });
}

function lookupType(typeName: string): ProtobufType {
  return root.lookupType(`${protoPackage}.${typeName}`);
}

function inferMessageName(
  value: unknown,
  messageType?: Function,
  packetName?: string,
  preferResponseType = false
): string {
  if (preferResponseType && packetName !== undefined && responseTypesByRequestPacketName[packetName] !== undefined) {
    return responseTypesByRequestPacketName[packetName];
  }
  if (packetName !== undefined && messageTypesByPacketName[packetName] !== undefined) {
    return messageTypesByPacketName[packetName];
  }
  if (messageType?.name !== undefined && hasType(messageType.name)) {
    return messageType.name;
  }
  const constructor = inferMessageType(value);
  if (constructor?.name !== undefined && inferredTypesByConstructorName.has(constructor.name)) {
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
  if (typeof payload.roomId === 'string' && 'state' in payload) {
    return 'MatchBingoRes';
  }
  if ('state' in payload) {
    return inferStateEnvelopeType(packetName);
  }
  if (typeof payload.accepted === 'boolean') {
    return 'AuthenticatePlayerRes';
  }
  if (typeof payload.actorType === 'string') {
    return 'EnsurePlayerActorRes';
  }
  if (Array.isArray(payload.delivered) && typeof payload.nextSeq === 'number') {
    return 'BingoNotificationBatch';
  }
  if (typeof payload.afterSeq === 'number') {
    return 'BingoNotificationsReq';
  }
  if (
    typeof payload.roomName === 'string' &&
    typeof payload.requiredPlayers === 'number' &&
    typeof payload.maxDrawNumber === 'number'
  ) {
    return 'BingoRoomSettingsPayload';
  }
  if (
    typeof payload.roomId === 'string' &&
    typeof payload.actorId === 'string' &&
    typeof payload.displayName === 'string'
  ) {
    return 'BingoRoomJoinReq';
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

function inferStateEnvelopeType(packetName: string | undefined): string {
  if (packetName !== undefined && messageTypesByPacketName[packetName] !== undefined) {
    return messageTypesByPacketName[packetName];
  }
  return 'BingoGameStartedNotify';
}

function inferMessageType(value: unknown): Function | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const constructor = Object.getPrototypeOf(value)?.constructor;
  return constructor === Object ? undefined : constructor;
}

function hasType(typeName: string): boolean {
  try {
    lookupType(typeName);
    return true;
  } catch {
    return false;
  }
}

function resolveProtoPath(): string {
  const candidates = [
    path.join(__dirname, 'bingo_messages.proto'),
    path.join(__dirname, '../../../Shared/Contracts/bingo_messages.proto')
  ];
  for (const candidate of candidates) {
    try {
      protobuf.loadSync(candidate);
      return candidate;
    } catch {
    }
  }
  return candidates[0];
}

export {
  bingoProtobufCodec,
  bingoPayloadBase64,
  fromBingoProto,
  toBingoProto
};
