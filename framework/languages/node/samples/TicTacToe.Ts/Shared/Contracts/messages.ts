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

function actorDisplayName(actorId) {
  return actorId === 'p1' ? 'Player X' : 'Player O';
}

function createJsonMessage(value) {
  return Message.from(Buffer.from(JSON.stringify(value)));
}

function readJsonMessage(message) {
  return JSON.parse(message.getString());
}

export { PacketNames, SampleNames, SampleTimings, actorDisplayName, createJsonMessage, readJsonMessage };
