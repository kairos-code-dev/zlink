const framework = require('../../../packages/framework/dist');
const nestjs = require('../../../packages/nestjs/dist');
const { TicTacToeBoard } = require('../shared/game');

class PlayerActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }
}

class PlayerActorFactory {
  async create(actorId, context) {
    return new PlayerActor(actorId, context);
  }
}

class GameSpot {
  constructor(context) {
    this.context = context;
    this.board = new TicTacToeBoard();
    this.players = new Map();
    this.moves = [];
  }

  join(playerId, mark) {
    this.players.set(playerId, mark);
  }

  place(playerId, cell) {
    const mark = this.players.get(playerId);
    if (mark === undefined) {
      throw new Error(`Player ${playerId} is not joined.`);
    }
    const winnerMark = this.board.place(playerId, mark, cell);
    this.moves.push({ playerId, cell, mark });
    if (winnerMark === undefined || winnerMark === null) {
      return undefined;
    }
    return [...this.players.entries()].find(([, value]) => value === winnerMark)?.[0];
  }
}

function createGameServer() {
  const channelEvents = [];
  const module = nestjs.ZLinkModule.forRoot({
    channels: { match: { client: { manualConnections: ['in-memory'] } } },
    spotNodes: ['game'],
    spotFactories: [GameSpot],
    actorFactories: { player: PlayerActorFactory }
  });
  const registration = getProvider(module, nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const channelClient = new framework.DefaultZLinkChannelClient(registration, {
    async send(channelName, packetName, message) {
      channelEvents.push({ kind: 'send', channelName, packetName, message: message.toString() });
    },
    async request(channelName, packetName, request) {
      channelEvents.push({ kind: 'request', channelName, packetName, request: request.toString() });
      return Buffer.from('match-ready');
    },
    async publish(channelName, topic, packetName, event) {
      channelEvents.push({ kind: 'publish', channelName, topic, packetName, event: event.toString() });
    }
  });
  const spots = getProvider(module, nestjs.ZLINK_SPOT_MANAGER);
  const actors = getProvider(module, nestjs.ZLINK_ACTOR_MANAGER);

  return { actors, channelClient, channelEvents, spots, GameSpot };
}

function getProvider(module, token) {
  const provider = module.providers.find((entry) => entry.provide === token);
  if (provider === undefined || provider.useValue === undefined) {
    throw new Error(`Sample provider is not registered: ${String(token)}`);
  }
  return provider.useValue;
}

module.exports = { createGameServer };
