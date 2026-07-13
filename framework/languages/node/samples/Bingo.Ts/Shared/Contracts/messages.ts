const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticatePlayerReq: 'AuthenticatePlayerReq',
  ensurePlayerActorReq: 'EnsurePlayerActorReq',
  matchBingoReq: 'MatchBingoReq',
  matchBingoApiReq: 'MatchBingoApiReq',
  allocateBingoRoom: 'AllocateBingoRoomReq',
  submitBingoCardReq: 'SubmitBingoCardReq',
  observeBingoEventsReq: 'ObserveBingoEventsReq',
  stopObservingBingoEventsReq: 'StopObservingBingoEventsReq',
  bingoRewardAcquiredEvent: 'BingoRewardAcquiredMsg',
  pingMsg: 'PingMsg',
  playerJoinedNotify: 'PlayerJoinedNotify',
  gameStartedNotify: 'BingoGameStartedNotify',
  numberDrawnNotify: 'BingoNumberDrawnNotify',
  stateNotify: 'BingoStateNotify',
  gameEndedNotify: 'BingoGameEndedNotify',
  rewardAnnouncedNotify: 'BingoRewardAnnouncedNotify'
});

const BingoModes = Object.freeze({
  twoPlayer: 'two-player'
});

const BingoSamplePlayers = Object.freeze({
  player1: 'player-1',
  player2: 'player-2',
  observer: 'observer'
});

const BingoRewardItems = Object.freeze({
  goldenDauberId: 'golden-dauber',
  goldenDauberName: 'Golden Dauber',
  legendaryRarity: 'Legendary'
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
  actorNodeRid: string;
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

export class MatchBingoApiReq {
  actorId: string;
  displayName: string;
  mode: BingoMode;
  actorNodeRid: string;

  constructor(actorId: string, displayName: string, mode: BingoMode, actorNodeRid: string) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.mode = mode;
    this.actorNodeRid = actorNodeRid;
  }
}

export interface MatchBingoApiRes {
  roomId: string;
  roomOwnerNodeRid: string;
}

export class AllocateBingoRoomReq {
  mode: BingoMode;

  constructor(mode: BingoMode) {
    this.mode = mode;
  }
}

export interface AllocateBingoRoomRes {
  roomId: string;
  roomOwnerNodeRid: string;
}

export interface BingoRoomSettingsPayload {
  roomName: string;
  mode: BingoMode;
  requiredPlayers: number;
  maxDrawNumber: number;
  purpose: string;
  observedRoomId: string | null;
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
  observeOnly: boolean;
}

export interface BingoRoomJoinRes {
  state: unknown;
}

export interface MatchBingoRes {
  roomId: string;
  state: unknown;
  roomOwnerNodeRid: string;
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

export class ObserveBingoEventsReq {
  roomId: string;

  constructor(roomId: string) {
    this.roomId = roomId;
  }
}

export interface ObserveBingoEventsRes {
  subscribed: boolean;
  observerNodeRid: string;
}

export class StopObservingBingoEventsReq {
  roomId: string;

  constructor(roomId: string) {
    this.roomId = roomId;
  }
}

export interface StopObservingBingoEventsRes {
  stopped: boolean;
  observerNodeRid: string;
}

export interface RejectedCommandRes {
  rejected: true;
  reason: string;
}

export interface PlayerIdentity {
  actorId: string;
  displayName: string;
}

export class PlayerJoinedNotify {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly displayName: string,
    readonly seat: number,
    readonly isHost: boolean,
    readonly state: unknown
  ) {}
}

export class BingoGameStartedNotify {
  constructor(readonly state: unknown) {}
}

export class BingoStateNotify {
  constructor(readonly state: unknown) {}
}

export class BingoGameEndedNotify {
  constructor(readonly state: unknown) {}
}

export type StateEnvelope = BingoGameStartedNotify | BingoStateNotify | BingoGameEndedNotify;

export interface RoomJoinError {
  error: string;
}

export class BingoNumberDrawnNotify {
  constructor(
    readonly roomId: string,
    readonly drawSeq: number,
    readonly number: number,
    readonly state: unknown
  ) {}
}

export type NumberDrawnNotify = BingoNumberDrawnNotify;

export class BingoRewardAcquiredMsg {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly drawSeq: number,
    readonly itemId: string,
    readonly itemName: string,
    readonly rarity: string
  ) {}
}

export class BingoRewardAnnouncedNotify implements BingoRewardAcquiredMsg {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly drawSeq: number,
    readonly itemId: string,
    readonly itemName: string,
    readonly rarity: string,
    readonly receivingSpotNodeRid: string
  ) {}
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

function authenticateSessionRes(actorId: string, displayName: string, actorNodeRid: string): AuthenticateSessionRes {
  return { actorId, displayName, actorNodeRid };
}

function matchBingoReq(mode: BingoMode = BingoModes.twoPlayer): MatchBingoReq {
  return new MatchBingoReq(mode);
}

function matchBingoApiReq(
  actorId: string,
  displayName: string,
  mode: BingoMode | undefined,
  actorNodeRid: string
): MatchBingoApiReq {
  return new MatchBingoApiReq(actorId, displayName, mode ?? BingoModes.twoPlayer, actorNodeRid);
}

function matchBingoApiRes(roomId: string, roomOwnerNodeRid: string): MatchBingoApiRes {
  return { roomId, roomOwnerNodeRid };
}

function allocateBingoRoomReq(mode: BingoMode = BingoModes.twoPlayer): AllocateBingoRoomReq {
  return new AllocateBingoRoomReq(mode);
}

function allocateBingoRoomRes(roomId: string, roomOwnerNodeRid: string): AllocateBingoRoomRes {
  return { roomId, roomOwnerNodeRid };
}

function bingoRoomSettingsPayload(settings: {
  roomName: string;
  mode: BingoMode;
  requiredPlayers: number;
  maxDrawNumber: number;
  purpose?: string;
  observedRoomId?: string | null;
}): BingoRoomSettingsPayload {
  return {
    roomName: settings.roomName,
    mode: settings.mode,
    requiredPlayers: settings.requiredPlayers,
    maxDrawNumber: settings.maxDrawNumber,
    purpose: settings.purpose ?? 'Game',
    observedRoomId: settings.observedRoomId ?? null
  };
}

function ensurePlayerActorReq(actorId: string, displayName: string): EnsurePlayerActorReq {
  return new EnsurePlayerActorReq(actorId, displayName);
}

function ensurePlayerActorRes(actor: BingoActorRef): EnsurePlayerActorRes {
  return {
    actorId: actor.actorId,
    actorType: 'bingo.player',
    actor
  };
}

function bingoRoomJoinReq(roomId: string, actorId: string, displayName: string, observeOnly = false): BingoRoomJoinReq {
  return { roomId, actorId, displayName, observeOnly };
}

function matchBingoRes(roomId: string, state: unknown, roomOwnerNodeRid: string): MatchBingoRes {
  return { roomId, state, roomOwnerNodeRid };
}

function submitBingoCardReq(roomId: string, card: number[]): SubmitBingoCardReq {
  return new SubmitBingoCardReq(roomId, card);
}

function submitBingoCardRes(state: unknown): SubmitBingoCardRes {
  return { state };
}

function observeBingoEventsReq(roomId: string): ObserveBingoEventsReq {
  return new ObserveBingoEventsReq(roomId);
}

function observeBingoEventsRes(subscribed: boolean, observerNodeRid: string): ObserveBingoEventsRes {
  return { subscribed, observerNodeRid };
}

function stopObservingBingoEventsReq(roomId: string): StopObservingBingoEventsReq {
  return new StopObservingBingoEventsReq(roomId);
}

function stopObservingBingoEventsRes(stopped: boolean, observerNodeRid: string): StopObservingBingoEventsRes {
  return { stopped, observerNodeRid };
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
  return new PlayerJoinedNotify(roomId, actor.actorId, actor.displayName, seat, isHost, state);
}

function roomJoinError(error: string): RoomJoinError {
  return { error };
}

function numberDrawnNotify(roomId: string, drawSeq: number, number: number, state: unknown): BingoNumberDrawnNotify {
  return new BingoNumberDrawnNotify(roomId, drawSeq, number, state);
}

function bingoRewardAcquiredEvent(value: BingoRewardAcquiredMsg): BingoRewardAcquiredMsg {
  return new BingoRewardAcquiredMsg(
    value.roomId,
    value.actorId,
    value.drawSeq,
    value.itemId,
    value.itemName,
    value.rarity
  );
}

function bingoRewardAnnouncedNotify(
  event: BingoRewardAcquiredMsg,
  receivingSpotNodeRid: string
): BingoRewardAnnouncedNotify {
  return new BingoRewardAnnouncedNotify(
    event.roomId,
    event.actorId,
    event.drawSeq,
    event.itemId,
    event.itemName,
    event.rarity,
    receivingSpotNodeRid
  );
}

export {
  BingoModes,
  BingoRewardItems,
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
  bingoRewardAcquiredEvent,
  bingoRewardAnnouncedNotify,
  bingoRoomJoinReq,
  bingoRoomSettingsPayload,
  deterministicCard,
  ensurePlayerActorReq,
  ensurePlayerActorRes,
  matchBingoApiRes,
  matchBingoReq,
  matchBingoApiReq,
  matchBingoRes,
  numberDrawnNotify,
  observeBingoEventsReq,
  observeBingoEventsRes,
  playerJoinedNotify,
  rejectedCommandRes,
  roomJoinError,
  stopObservingBingoEventsReq,
  stopObservingBingoEventsRes,
  submitBingoCardReq,
  submitBingoCardRes,
  withPlayerIdentity
};
