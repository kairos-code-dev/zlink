const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticateRes: 'AuthenticateRes',
  authenticatePlayerReq: 'AuthenticatePlayerReq',
  authenticatePlayerRes: 'AuthenticatePlayerRes',
  createGameReq: 'CreateGameReq',
  createGameHttpReq: 'CreateGameHttpReq',
  createGameHttpRes: 'CreateGameHttpRes',
  joinGameReq: 'JoinGameReq',
  joinGameRes: 'JoinGameRes',
  observeMilestoneReq: 'ObserveMilestoneReq',
  observeMilestoneRes: 'ObserveMilestoneRes',
  placeMarkReq: 'PlaceMarkReq',
  placeMarkRes: 'PlaceMarkRes',
  leaveGameReq: 'LeaveGameReq',
  playerJoinedNotify: 'PlayerJoinedNotify',
  gameStateNotify: 'GameStateNotify',
  winMilestoneNotify: 'WinMilestoneNotify',
  playerWinMilestoneEvent: 'PlayerWinMilestoneEvent'
});

export interface AuthenticateReq {
  accessToken: string;
}

export class AuthenticateReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
}

export interface AuthenticatePlayerReq {
  accessToken: string;
}

export class AuthenticatePlayerReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
}

export interface AuthenticatePlayerRes {
  player: PlayerInfo;
}

export interface CreateGameReq {
  gameName: string;
}

export class CreateGameHttpReq {
  gameName?: string;

  constructor(gameName?: string) {
    if (gameName !== undefined) {
      this.gameName = gameName;
    }
  }
}

export class CreateGameReq implements CreateGameReq {
  gameName: string;

  constructor(gameName: string) {
    this.gameName = gameName;
  }
}

export interface CreateGameRes {
  roomId: string;
  gameName: string;
  ownerPlayEndpoint: string;
  playEndpoints: string[];
  playNodes: PlayNodeInfo[];
  requiredLevel: number;
}

export interface CreateGameHttpRes {
  roomId: string;
  gameName: string;
  ownerPlayEndpoint: string;
  playEndpoints: string[];
  playNodes: PlayNodeInfo[];
  requiredLevel: number;
}

export interface AuthenticateRes {
  player: PlayerInfo;
}

export interface PlayerInfo {
  actorId: string;
  displayName: string;
  level: number;
  wins: number;
}

export interface PlayNodeInfo {
  streamEndpoint: string;
  spotNodeRid: string;
}

export interface JoinGameReq {
  roomId: string;
}

export class JoinGameReq {
  roomId: string;

  constructor(roomId: string) {
    this.roomId = roomId;
  }
}

export interface JoinGameRes {
  state: GameState;
}

export interface TicTacToeGameJoinReq {
  roomId: string;
  player: PlayerInfo;
}

export interface TicTacToeGameJoinRes {
  state: GameState;
}

export class ObserveMilestoneReq {
}

export interface ObserveMilestoneRes {
  subscribed: boolean;
}

export class PlaceMarkReq {
  constructor(readonly cell: number) {}
}

export interface PlaceMarkRes {
  state: GameState;
}

export class LeaveGameReq {
  constructor(readonly roomId: string) {}
}

export class PlayerJoinedNotify {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly displayName: string,
    readonly level: number,
    readonly mark: string,
    readonly state: GameState
  ) {}
}

export enum GameStatus {
  WaitingForPlayers = 'WaitingForPlayers',
  InProgress = 'InProgress',
  Won = 'Won',
  Draw = 'Draw',
  TurnTimedOut = 'TurnTimedOut'
}

export const GameMarks = Object.freeze({
  x: 'X',
  o: 'O'
});

export interface GameState {
  roomId: string;
  board: string;
  status: GameStatus;
  winner: string | null;
  nextTurn: string;
  xActorId: string | null;
  oActorId: string | null;
  lastMoveActorId: string | null;
  lastMoveCell: number | null;
}

export class GameStateNotify {
  constructor(readonly state: GameState) {}
}

export class WinMilestoneNotify {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly displayName: string,
    readonly wins: number,
    readonly receivingSpotNodeRid: string
  ) {}
}

export class PlayerWinMilestoneEvent {
  constructor(
    readonly roomId: string,
    readonly actorId: string,
    readonly displayName: string,
    readonly wins: number
  ) {}
}

export interface TicTacToeActor {
  actorId: string;
  displayName: string;
  level: number;
  wins: number;
  roomId?: string;
  attachClient(client: TicTacToeActorClient): void;
  detachClient(client: TicTacToeActorClient): void;
  markDisconnected(): void;
  markForDestroyAfterRoomLeave(): void;
  destroyAfterEntrySpotJoin: boolean;
  push(payload: PlayerJoinedNotify | GameStateNotify | WinMilestoneNotify): Promise<void>;
}

export interface TicTacToeActorClient {
  send(message: unknown): {
    metadata(key: string, value: string): {
      submit(): Promise<void>;
    };
  };
}

function actorDisplayName(actorId: string): string {
  if (actorId === 'player-x') {
    return 'Player X';
  }
  if (actorId === 'player-o') {
    return 'Player O';
  }
  return 'Observer';
}

function playerInfo(accessToken: string): PlayerInfo {
  return {
    actorId: accessToken,
    displayName: actorDisplayName(accessToken),
    level: 3,
    wins: accessToken === 'player-x' ? 99 : 0
  };
}

function authenticateReq(accessToken: string): AuthenticateReq {
  return new AuthenticateReq(accessToken);
}

function authenticatePlayerReq(accessToken: string): AuthenticatePlayerReq {
  return new AuthenticatePlayerReq(accessToken);
}

function authenticatePlayerRes(accessToken: string): AuthenticatePlayerRes {
  return {
    player: playerInfo(accessToken)
  };
}

function createGameReq(gameName: string): CreateGameReq {
  return new CreateGameReq(gameName);
}

function createGameHttpReq(gameName?: string): CreateGameHttpReq {
  return new CreateGameHttpReq(gameName);
}

function createGameRes(
  roomId: string,
  gameName: string,
  ownerPlayEndpoint: string,
  playEndpoints: string[],
  playNodes: PlayNodeInfo[],
  requiredLevel: number
): CreateGameRes {
  return { roomId, gameName, ownerPlayEndpoint, playEndpoints, playNodes, requiredLevel };
}

function createGameHttpRes(response: CreateGameRes): CreateGameHttpRes {
  return {
    roomId: response.roomId,
    gameName: response.gameName,
    ownerPlayEndpoint: response.ownerPlayEndpoint,
    playEndpoints: response.playEndpoints,
    playNodes: response.playNodes,
    requiredLevel: response.requiredLevel
  };
}

function authenticateRes(player: PlayerInfo): AuthenticateRes {
  return { player };
}

function joinGameReq(roomId: string): JoinGameReq {
  return new JoinGameReq(roomId);
}

function joinGameRes(state: GameState): JoinGameRes {
  return { state };
}

function observeMilestoneRes(subscribed: boolean): ObserveMilestoneRes {
  return { subscribed };
}

function placeMarkStreamReq(cell: number): PlaceMarkReq {
  return new PlaceMarkReq(cell);
}

function placeMarkRes(state: GameState): PlaceMarkRes {
  return { state };
}

function playerJoinedNotify(
  roomId: string,
  actorId: string,
  displayName: string,
  level: number,
  mark: string,
  state: GameState
): PlayerJoinedNotify {
  return new PlayerJoinedNotify(roomId, actorId, displayName, level, mark, state);
}

function gameStateNotify(state: GameState): GameStateNotify {
  return new GameStateNotify(state);
}

function playerWinMilestoneEvent(
  roomId: string,
  actorId: string,
  displayName: string,
  wins: number
): PlayerWinMilestoneEvent {
  return new PlayerWinMilestoneEvent(roomId, actorId, displayName, wins);
}

function winMilestoneNotify(
  event: PlayerWinMilestoneEvent,
  receivingSpotNodeRid: string
): WinMilestoneNotify {
  return new WinMilestoneNotify(
    event.roomId,
    event.actorId,
    event.displayName,
    event.wins,
    receivingSpotNodeRid
  );
}

export {
  PacketNames,
  actorDisplayName,
  authenticatePlayerReq,
  authenticatePlayerRes,
  authenticateReq,
  authenticateRes,
  createGameRes,
  createGameHttpRes,
  createGameHttpReq,
  createGameReq,
  gameStateNotify,
  joinGameReq,
  joinGameRes,
  observeMilestoneRes,
  placeMarkRes,
  placeMarkStreamReq,
  playerInfo,
  playerWinMilestoneEvent,
  playerJoinedNotify,
  winMilestoneNotify
};
