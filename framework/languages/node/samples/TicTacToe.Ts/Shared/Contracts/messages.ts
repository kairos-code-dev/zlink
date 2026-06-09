const { Message } = require('@zlink-systems/zlink');

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
  placeMarkReq: 'PlaceMarkReq',
  placeMarkRes: 'PlaceMarkRes',
  playerJoinedNotify: 'PlayerJoinedNotify',
  gameStateNotify: 'GameStateNotify'
});

const SampleNames = Object.freeze({
  apiChannel: 'tictactoe.api',
  playChannel: 'tictactoe.play',
  clientStreamNode: 'client.stream',
  playerActorType: 'player',
  playActorNodeRid: 'tictactoe.play.node'
});

const SampleTimings = Object.freeze({
  requestTimeout: 7000
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

export interface AuthenticatePlayerRes {
  actorId: string;
  displayName: string;
}

export interface CreateGameReq {
  gameName?: string;
}

export interface CreateGameRes {
  roomId: string;
  gameName: string;
  playEndpoint: string;
}

export interface CreateGameHttpRes {
  roomId: string;
  gameName: string;
  playEndpoint: string;
}

export interface AuthenticateRes {
  actorId: string;
  displayName: string;
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

export interface JoinGameInternalReq {
  actor: TicTacToeActor;
  roomId: string;
}

export interface JoinGameRes {
  roomId: string;
  actorId: string;
  mark: string;
  state: unknown;
}

export interface PlaceMarkStreamReq {
  cell: number;
}

export class PlaceMarkReq {
  constructor(readonly cell: number) {}
}

export interface PlaceMarkInternalReq {
  actor: TicTacToeActor;
  cell: number;
}

export interface PlaceMarkRes {
  roomId: string;
  actorId: string;
  cell: number;
  state: unknown;
}

export interface PlayerJoinedNotify {
  roomId: string;
  actorId: string;
  mark: string;
  state: unknown;
}

export interface GameState {
  roomId: string;
  board: string;
  status: string;
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

export interface TicTacToeActor {
  actorId: string;
  displayName: string;
}

type ZLinkMessage = {
  getString(): string;
  close(): void;
};

function actorDisplayName(actorId: string): string {
  return actorId === 'p1' ? 'Player X' : 'Player O';
}

function createJsonMessage(value: unknown): ZLinkMessage {
  return Message.from(Buffer.from(JSON.stringify(value)));
}

function readJsonMessage<TValue = unknown>(message: ZLinkMessage): TValue {
  return JSON.parse(message.getString());
}

function authenticateReq(accessToken: string): AuthenticateReq {
  return new AuthenticateReq(accessToken);
}

function authenticatePlayerRes(accessToken: string): AuthenticatePlayerRes {
  return {
    actorId: accessToken,
    displayName: actorDisplayName(accessToken)
  };
}

function createGameReq(gameName: string | undefined): CreateGameReq {
  return { gameName };
}

function createGameRes(roomId: string, gameName: string, playEndpoint: string): CreateGameRes {
  return { roomId, gameName, playEndpoint };
}

function createGameHttpRes(response: CreateGameRes): CreateGameHttpRes {
  return {
    roomId: response.roomId,
    gameName: response.gameName,
    playEndpoint: response.playEndpoint
  };
}

function authenticateRes(actorId: string, displayName: string): AuthenticateRes {
  return { actorId, displayName };
}

function joinGameReq(roomId: string): JoinGameReq {
  return new JoinGameReq(roomId);
}

function joinGameInternalReq(actor: TicTacToeActor, roomId: string): JoinGameInternalReq {
  return { actor, roomId };
}

function joinGameRes(roomId: string, actorId: string, mark: string, state: unknown): JoinGameRes {
  return { roomId, actorId, mark, state };
}

function placeMarkStreamReq(cell: number): PlaceMarkStreamReq {
  return new PlaceMarkReq(cell);
}

function placeMarkReq(actor: TicTacToeActor, cell: number): PlaceMarkInternalReq {
  return { actor, cell };
}

function placeMarkRes(roomId: string, actorId: string, cell: number, state: unknown): PlaceMarkRes {
  return { roomId, actorId, cell, state };
}

function playerJoinedNotify(roomId: string, actorId: string, mark: string, state: unknown): PlayerJoinedNotify {
  return { roomId, actorId, mark, state };
}

function gameStateNotify(state: GameState): GameStateNotify {
  return { state };
}

export {
  PacketNames,
  SampleNames,
  SampleTimings,
  actorDisplayName,
  authenticatePlayerRes,
  authenticateReq,
  authenticateRes,
  createGameRes,
  createGameHttpRes,
  createGameReq,
  createJsonMessage,
  gameStateNotify,
  joinGameInternalReq,
  joinGameReq,
  joinGameRes,
  placeMarkRes,
  placeMarkReq,
  placeMarkStreamReq,
  playerJoinedNotify,
  readJsonMessage
};
