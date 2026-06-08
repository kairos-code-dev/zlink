const { Message } = require('@zlink-systems/zlink');

const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticatePlayerReq: 'AuthenticatePlayerReq',
  ensurePlayerActorReq: 'EnsurePlayerActorReq',
  matchBingoReq: 'MatchBingoReq',
  matchBingoApiReq: 'MatchBingoApiReq',
  allocateBingoRoom: 'AllocateBingoRoom',
  startBingoGameReq: 'StartBingoGameReq',
  bingoNotificationsReq: 'BingoNotificationsReq',
  ping: 'Ping',
  playerJoinedNotify: 'PlayerJoinedNotify',
  gameStartedNotify: 'BingoGameStartedNotify',
  numberDrawnNotify: 'BingoNumberDrawnNotify',
  stateNotify: 'BingoStateNotify',
  gameEndedNotify: 'BingoGameEndedNotify'
});

const BingoModes = Object.freeze({
  fourPlayer: 'four-player'
});

export type BingoMode = typeof BingoModes.fourPlayer;

export interface AuthenticateReq {
  accessToken: string;
}

export interface AuthenticatePlayerRes {
  accepted: boolean;
  actorId: string | null;
  displayName: string | null;
  reason: string | null;
}

export interface AuthenticateSessionRes {
  actorId: string;
  displayName: string;
}

export interface MatchBingoReq {
  mode?: BingoMode;
}

export interface MatchBingoApiRes {
  roomId: string;
}

export interface AllocateBingoRoomReq {
  mode: BingoMode;
}

export interface AllocateBingoRoomRes {
  roomId: string;
}

export interface EnsurePlayerActorReq {
  actorId: string;
  displayName: string;
}

export interface BingoActorRef {
  nodeRid: string;
  actorId: string;
  generation: number;
}

export interface EnsurePlayerActorRes {
  actorId: string;
  actorType: string;
  actor: BingoActorRef;
}

export interface StartBingoGameReq {
  roomId: string;
}

export interface BingoRoomJoinReq {
  roomId: string;
  actorId: string;
  displayName: string;
}

export interface MatchBingoRes {
  roomId: string;
  state: unknown;
}

export interface BingoNotificationsReq {
  afterSeq: number;
}

export interface RejectedCommandRes {
  rejected: true;
  reason: string;
}

export interface PlayerIdentity {
  actorId: string;
  displayName: string;
}

export interface PlayerJoinedNotify {
  roomId: string;
  actorId: string;
  displayName: string;
  seat: number;
  isHost: boolean;
  state: unknown;
}

export interface StateEnvelope {
  state: unknown;
}

export interface RoomJoinError {
  error: string;
}

export interface NumberDrawnNotify {
  roomId: string;
  drawSeq: number;
  number: number;
  state: unknown;
}

type ZLinkMessage = {
  getString(): string;
  close(): void;
};

function actorDisplayName(actorId: string): string {
  return actorId.replace('player-', 'Player ');
}

function deterministicCard(actorId: string): number[] {
  const base = {
    'player-1': [1, 2, 3, 4, 5],
    'player-2': [6, 7, 8, 9, 10],
    'player-3': [1, 2, 3, 4, 5],
    'player-4': [11, 12, 13, 14, 15]
  }[actorId] ?? [16, 17, 18, 19, 20];
  const card = [];
  for (let row = 0; row < 5; row += 1) {
    for (let col = 0; col < 5; col += 1) {
      card.push(row === 2 && col === 2 ? 0 : base[col] + row * 20);
    }
  }
  return card;
}

function createJsonMessage(value: unknown): ZLinkMessage {
  return Message.from(Buffer.from(JSON.stringify(value)));
}

function readJsonMessage<TValue = unknown>(message: ZLinkMessage): TValue {
  return JSON.parse(message.getString());
}

function authenticateReq(accessToken: string): AuthenticateReq {
  return { accessToken };
}

function authenticatePlayerAccepted(accessToken: string): AuthenticatePlayerRes {
  return {
    accepted: true,
    actorId: accessToken,
    displayName: actorDisplayName(accessToken),
    reason: null
  };
}

function authenticatePlayerRejected(reason: string): AuthenticatePlayerRes {
  return {
    accepted: false,
    actorId: null,
    displayName: null,
    reason
  };
}

function authenticateSessionRes(actorId: string, displayName: string): AuthenticateSessionRes {
  return { actorId, displayName };
}

function matchBingoReq(mode: BingoMode | undefined = BingoModes.fourPlayer): MatchBingoReq {
  return { mode };
}

function matchBingoApiRes(roomId: string): MatchBingoApiRes {
  return { roomId };
}

function allocateBingoRoomReq(mode: BingoMode | undefined = BingoModes.fourPlayer): AllocateBingoRoomReq {
  return { mode };
}

function allocateBingoRoomRes(roomId: string): AllocateBingoRoomRes {
  return { roomId };
}

function ensurePlayerActorReq(actorId: string, displayName: string): EnsurePlayerActorReq {
  return { actorId, displayName };
}

function ensurePlayerActorRes(actor: { actorId: string }): EnsurePlayerActorRes {
  return {
    actorId: actor.actorId,
    actorType: 'bingo.player',
    actor: {
      nodeRid: 'bingo.room.node',
      actorId: actor.actorId,
      generation: 1
    }
  };
}

function startBingoGameReq(roomId: string): StartBingoGameReq {
  return { roomId };
}

function bingoRoomJoinReq(roomId: string, actorId: string, displayName: string): BingoRoomJoinReq {
  return { roomId, actorId, displayName };
}

function matchBingoRes(roomId: string, state: unknown): MatchBingoRes {
  return { roomId, state };
}

function bingoNotificationsReq(afterSeq: number): BingoNotificationsReq {
  return { afterSeq };
}

function rejectedCommandRes(reason: string): RejectedCommandRes {
  return { rejected: true, reason };
}

function withPlayerIdentity<TRequest extends object>(
  request: TRequest,
  actorId: string,
  displayName: string
): TRequest & PlayerIdentity {
  return {
    ...request,
    actorId,
    displayName
  };
}

function playerJoinedNotify(
  roomId: string,
  actor: { actorId: string; displayName: string },
  seat: number,
  isHost: boolean,
  state: unknown
): PlayerJoinedNotify {
  return {
    roomId,
    actorId: actor.actorId,
    displayName: actor.displayName,
    seat,
    isHost,
    state
  };
}

function stateEnvelope(state: unknown): StateEnvelope {
  return { state };
}

function roomJoinError(error: string): RoomJoinError {
  return { error };
}

function numberDrawnNotify(roomId: string, drawSeq: number, number: number, state: unknown): NumberDrawnNotify {
  return { roomId, drawSeq, number, state };
}

export {
  BingoModes,
  PacketNames,
  actorDisplayName,
  allocateBingoRoomReq,
  allocateBingoRoomRes,
  authenticatePlayerAccepted,
  authenticatePlayerRejected,
  authenticateReq,
  authenticateSessionRes,
  bingoRoomJoinReq,
  bingoNotificationsReq,
  createJsonMessage,
  deterministicCard,
  ensurePlayerActorReq,
  ensurePlayerActorRes,
  matchBingoApiRes,
  matchBingoReq,
  matchBingoRes,
  numberDrawnNotify,
  playerJoinedNotify,
  readJsonMessage,
  rejectedCommandRes,
  roomJoinError,
  startBingoGameReq,
  stateEnvelope,
  withPlayerIdentity
};
