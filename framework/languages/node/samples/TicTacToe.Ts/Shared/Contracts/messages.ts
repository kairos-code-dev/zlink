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

export interface AuthenticatePlayerRes {
  actorId: string;
  displayName: string;
}

export interface CreateGameReq {
  gameName?: string;
}

export interface CreateGameRes {
  gameId: string;
  gameName: string;
  playEndpoint: string;
}

export interface CreateGameHttpRes {
  gameId: string;
  gameName: string;
  playEndpoint: string;
}

export interface AuthenticateRes {
  actorId: string;
  displayName: string;
}

export interface JoinGameReq {
  gameId: string;
}

export interface JoinGameInternalReq {
  actor: TicTacToeActor;
  gameId: string;
}

export interface JoinGameRes {
  gameId: string;
  actorId: string;
  mark: string;
  state: unknown;
}

export interface PlaceMarkStreamReq {
  gameId: string;
  cell: number;
}

export interface PlaceMarkReq {
  actor: TicTacToeActor;
  gameId: string;
  cell: number;
}

export interface PlaceMarkRes {
  gameId: string;
  actorId: string;
  cell: number;
  state: unknown;
}

export interface PlayerJoinedNotify {
  gameId: string;
  actorId: string;
  players: string[];
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
  return { accessToken };
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

function createGameRes(gameId: string, gameName: string, playEndpoint: string): CreateGameRes {
  return { gameId, gameName, playEndpoint };
}

function createGameHttpRes(response: CreateGameRes): CreateGameHttpRes {
  return {
    gameId: response.gameId,
    gameName: response.gameName,
    playEndpoint: response.playEndpoint
  };
}

function authenticateRes(actorId: string, displayName: string): AuthenticateRes {
  return { actorId, displayName };
}

function joinGameReq(gameId: string): JoinGameReq {
  return { gameId };
}

function joinGameInternalReq(actor: TicTacToeActor, gameId: string): JoinGameInternalReq {
  return { actor, gameId };
}

function joinGameRes(gameId: string, actorId: string, mark: string, state: unknown): JoinGameRes {
  return { gameId, actorId, mark, state };
}

function placeMarkStreamReq(gameId: string, cell: number): PlaceMarkStreamReq {
  return { gameId, cell };
}

function placeMarkReq(actor: TicTacToeActor, gameId: string, cell: number): PlaceMarkReq {
  return { actor, gameId, cell };
}

function placeMarkRes(gameId: string, actorId: string, cell: number, state: unknown): PlaceMarkRes {
  return { gameId, actorId, cell, state };
}

function playerJoinedNotify(gameId: string, actorId: string, players: string[]): PlayerJoinedNotify {
  return { gameId, actorId, players };
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
  joinGameInternalReq,
  joinGameReq,
  joinGameRes,
  placeMarkRes,
  placeMarkReq,
  placeMarkStreamReq,
  playerJoinedNotify,
  readJsonMessage
};
