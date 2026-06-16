const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticatePlayerReq: 'AuthenticatePlayerReq',
  ensurePlayerActorReq: 'EnsurePlayerActorReq',
  matchBingoReq: 'MatchBingoReq',
  matchBingoApiReq: 'MatchBingoApiReq',
  allocateBingoRoom: 'AllocateBingoRoomReq',
  submitBingoCardReq: 'SubmitBingoCardReq',
  bingoNotificationsReq: 'BingoNotificationsReq',
  ping: 'Ping',
  playerJoinedNotify: 'PlayerJoinedNotify',
  gameStartedNotify: 'BingoGameStartedNotify',
  numberDrawnNotify: 'BingoNumberDrawnNotify',
  stateNotify: 'BingoStateNotify',
  gameEndedNotify: 'BingoGameEndedNotify'
});

const BingoModes = Object.freeze({
  twoPlayer: 'two-player'
});

const BingoSamplePlayers = Object.freeze({
  player1: 'player-1',
  player2: 'player-2'
});

export type BingoMode = typeof BingoModes.twoPlayer;

export enum BingoRoomStatus {
  WaitingForPlayers = 'WaitingForPlayers',
  Running = 'Running',
  Finished = 'Finished'
}

export class AuthenticateReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
}

export class AuthenticatePlayerReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
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

export class MatchBingoReq {
  mode?: BingoMode;
  actorId?: string;
  displayName?: string;

  constructor(mode?: BingoMode) {
    if (mode !== undefined) {
      this.mode = mode;
    }
  }
}

export interface MatchBingoApiRes {
  roomId: string;
}

export class AllocateBingoRoomReq {
  mode: BingoMode;

  constructor(mode: BingoMode) {
    this.mode = mode;
  }
}

export interface AllocateBingoRoomRes {
  roomId: string;
}

export interface BingoRoomSettingsPayload {
  roomName: string;
  mode: BingoMode;
  requiredPlayers: number;
  maxDrawNumber: number;
}

export class EnsurePlayerActorReq {
  actorId: string;
  displayName: string;

  constructor(actorId: string, displayName: string) {
    this.actorId = actorId;
    this.displayName = displayName;
  }
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

export interface BingoRoomJoinReq {
  roomId: string;
  actorId: string;
  displayName: string;
}

export interface MatchBingoRes {
  roomId: string;
  state: unknown;
}

export class SubmitBingoCardReq {
  roomId: string;
  card: number[];
  actorId?: string;
  displayName?: string;

  constructor(roomId: string, card: number[]) {
    this.roomId = roomId;
    this.card = card;
  }
}

export interface SubmitBingoCardRes {
  state: unknown;
}

export class BingoNotificationsReq {
  afterSeq: number;
  actorId?: string;
  displayName?: string;

  constructor(afterSeq: number) {
    this.afterSeq = afterSeq;
  }
}

export interface BingoNotificationFrame {
  seq: number;
  actorId: string;
  packetName: string;
  payloadBase64: string;
}

export interface BingoNotificationBatch {
  nextSeq: number;
  delivered: BingoNotificationFrame[];
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

function actorDisplayName(actorId: string): string {
  return actorId.replace('player-', 'Player ');
}

function deterministicCard(actorId: string): number[] {
  return {
    [BingoSamplePlayers.player1]: [1, 2, 3, 4, 0, 6, 7, 8, 9],
    [BingoSamplePlayers.player2]: [10, 11, 12, 13, 0, 14, 15, 1, 2]
  }[actorId] ?? [1, 3, 5, 7, 0, 9, 11, 13, 15];
}

function authenticateReq(accessToken: string): AuthenticateReq {
  return new AuthenticateReq(accessToken);
}

function authenticatePlayerReq(accessToken: string): AuthenticatePlayerReq {
  return new AuthenticatePlayerReq(accessToken);
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

function matchBingoReq(mode: BingoMode | undefined = BingoModes.twoPlayer): MatchBingoReq {
  return new MatchBingoReq(mode);
}

function matchBingoApiRes(roomId: string): MatchBingoApiRes {
  return { roomId };
}

function allocateBingoRoomReq(mode: BingoMode | undefined = BingoModes.twoPlayer): AllocateBingoRoomReq {
  return new AllocateBingoRoomReq(mode);
}

function allocateBingoRoomRes(roomId: string): AllocateBingoRoomRes {
  return { roomId };
}

function bingoRoomSettingsPayload(settings: {
  roomName: string;
  mode: BingoMode;
  requiredPlayers: number;
  maxDrawNumber: number;
}): BingoRoomSettingsPayload {
  return {
    roomName: settings.roomName,
    mode: settings.mode,
    requiredPlayers: settings.requiredPlayers,
    maxDrawNumber: settings.maxDrawNumber
  };
}

function ensurePlayerActorReq(actorId: string, displayName: string): EnsurePlayerActorReq {
  return new EnsurePlayerActorReq(actorId, displayName);
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

function bingoRoomJoinReq(roomId: string, actorId: string, displayName: string): BingoRoomJoinReq {
  return { roomId, actorId, displayName };
}

function matchBingoRes(roomId: string, state: unknown): MatchBingoRes {
  return { roomId, state };
}

function submitBingoCardReq(roomId: string, card: number[]): SubmitBingoCardReq {
  return new SubmitBingoCardReq(roomId, card);
}

function submitBingoCardRes(state: unknown): SubmitBingoCardRes {
  return { state };
}

function bingoNotificationsReq(afterSeq: number): BingoNotificationsReq {
  return new BingoNotificationsReq(afterSeq);
}

function bingoNotificationBatch(value: {
  delivered: BingoNotificationFrame[];
  nextSeq: number;
}): BingoNotificationBatch {
  return {
    nextSeq: value.nextSeq,
    delivered: value.delivered
  };
}

function rejectedCommandRes(reason: string): RejectedCommandRes {
  return { rejected: true, reason };
}

function withPlayerIdentity<TRequest extends object>(
  request: TRequest,
  actorId: string,
  displayName: string
): TRequest & PlayerIdentity {
  const value = request as TRequest & Partial<PlayerIdentity>;
  value.actorId = actorId;
  value.displayName = displayName;
  return value as TRequest & PlayerIdentity;
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
  BingoSamplePlayers,
  PacketNames,
  actorDisplayName,
  allocateBingoRoomReq,
  allocateBingoRoomRes,
  authenticatePlayerAccepted,
  authenticatePlayerRejected,
  authenticatePlayerReq,
  authenticateReq,
  authenticateSessionRes,
  bingoRoomJoinReq,
  bingoRoomSettingsPayload,
  bingoNotificationsReq,
  bingoNotificationBatch,
  deterministicCard,
  ensurePlayerActorReq,
  ensurePlayerActorRes,
  matchBingoApiRes,
  matchBingoReq,
  matchBingoRes,
  numberDrawnNotify,
  playerJoinedNotify,
  rejectedCommandRes,
  roomJoinError,
  stateEnvelope,
  submitBingoCardReq,
  submitBingoCardRes,
  withPlayerIdentity
};
