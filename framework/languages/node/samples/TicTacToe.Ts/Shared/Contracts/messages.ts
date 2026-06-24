const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticateRes: 'AuthenticateRes',
  authenticatePlayerReq: 'AuthenticatePlayerReq',
  authenticatePlayerRes: 'AuthenticatePlayerRes',
  createGame: 'CreateGame',
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
  gameName?: string;
}

export class CreateGame implements CreateGameReq {
  gameName?: string;

  constructor(gameName?: string) {
    if (gameName !== undefined) {
      this.gameName = gameName;
    }
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
  player?: PlayerInfo;
}

export class JoinGameReq {
  roomId: string;

  constructor(roomId: string) {
    this.roomId = roomId;
  }
}

export interface JoinGameRes {
  state: unknown;
}

export interface TicTacToeGameJoinReq {
  roomId: string;
  player: PlayerInfo;
}

export class ObserveMilestoneReq {
}

export interface ObserveMilestoneRes {
  subscribed: boolean;
}

export interface PlaceMarkStreamReq {
  cell: number;
}

export class PlaceMarkReq {
  constructor(readonly cell: number) {}
}

export interface PlaceMarkRes {
  state: unknown;
}

export class LeaveGameReq {
  constructor(readonly roomId: string) {}
}

export interface PlayerJoinedNotify {
  roomId: string;
  actorId: string;
  displayName: string;
  level: number;
  mark: string;
  state: unknown;
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
  nextTurn: string | null;
  xActorId: string | null;
  oActorId: string | null;
  lastMoveActorId: string | null;
  lastMoveCell: number | null;
}

export interface GameStateNotify {
  state: GameState;
}

export interface WinMilestoneNotify {
  roomId: string;
  actorId: string;
  displayName: string;
  wins: number;
  receivingSpotNodeRid: string;
}

export interface PlayerWinMilestoneEvent {
  roomId: string;
  actorId: string;
  displayName: string;
  wins: number;
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
  push(packetName: string, payload: unknown): Promise<void>;
}

export interface TicTacToeActorClient {
  send(message: unknown): {
    packetName(packetName: string): {
      metadata(key: string, value: string): {
        submit(signal?: AbortSignal): Promise<void>;
      };
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

function createGameReq(gameName?: string): CreateGameReq {
  return new CreateGame(gameName);
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

function joinGameRes(state: unknown): JoinGameRes {
  return { state };
}

function observeMilestoneRes(subscribed: boolean): ObserveMilestoneRes {
  return { subscribed };
}

function placeMarkStreamReq(cell: number): PlaceMarkStreamReq {
  return new PlaceMarkReq(cell);
}

function placeMarkRes(state: unknown): PlaceMarkRes {
  return { state };
}

function playerJoinedNotify(
  roomId: string,
  actorId: string,
  displayName: string,
  level: number,
  mark: string,
  state: unknown
): PlayerJoinedNotify {
  return { roomId, actorId, displayName, level, mark, state };
}

function gameStateNotify(state: GameState): GameStateNotify {
  return { state };
}

function playerWinMilestoneEvent(
  roomId: string,
  actorId: string,
  displayName: string,
  wins: number
): PlayerWinMilestoneEvent {
  return { roomId, actorId, displayName, wins };
}

function winMilestoneNotify(
  event: PlayerWinMilestoneEvent,
  receivingSpotNodeRid: string
): WinMilestoneNotify {
  return { ...event, receivingSpotNodeRid };
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
