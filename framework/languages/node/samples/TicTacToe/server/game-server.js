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
    this.notifications = [];
    this.timerRegistered = false;
  }

  async onInitialize() {
    this.timer = await this.context.addTimer('turn-timeout', 1000, TurnTimeoutTimer, {
      stopOnUnhandledException: true
    });
    this.timerRegistered = true;
  }

  join(playerId, mark) {
    this.players.set(playerId, mark);
    if (this.players.size === 2) {
      const opponent = [...this.players.keys()].find((id) => id !== playerId);
      this.notifications.push({ packetName: 'PlayerJoinedNotify', target: opponent, joined: playerId });
    }
  }

  place(playerId, cell) {
    const mark = this.players.get(playerId);
    if (mark === undefined) {
      throw new Error(`Player ${playerId} is not joined.`);
    }
    const winnerMark = this.board.place(playerId, mark, cell);
    this.moves.push({ playerId, cell, mark });
    this.notifyGameState(playerId, winnerMark === undefined || winnerMark === null
      ? undefined
      : [...this.players.entries()].find(([, value]) => value === winnerMark)?.[0]);
    if (winnerMark === undefined || winnerMark === null) {
      return undefined;
    }
    return [...this.players.entries()].find(([, value]) => value === winnerMark)?.[0];
  }

  notifyGameState(excludedPlayerId, winner) {
    for (const target of this.players.keys()) {
      if (target !== excludedPlayerId) {
        this.notifications.push({ packetName: 'GameStateNotify', target, winner: winner ?? null });
      }
    }
  }
}

class TurnTimeoutTimer {
  async handle() {}
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

async function playDeterministicGame(server) {
  const match = await server.channelClient
    .requestToChannel('match', Buffer.from('start'))
    .packetName('CreateMatch')
    .timeout(1000)
    .submit();
  const created = await server.spots.create(server.GameSpot);
  const p1 = await server.actors.getOrCreate('p1', 'player');
  const p2 = await server.actors.getOrCreate('p2', 'player');

  const result = {};
  await server.spots.executeOnSpot(created.spotRid, (spot) => {
    spot.join(p1.actorId, 'X');
    spot.join(p2.actorId, 'O');
    spot.place('p1', 0);
    spot.place('p2', 3);
    spot.place('p1', 1);
    spot.place('p2', 4);
    result.winner = spot.place('p1', 2);
    result.timerRegistered = spot.timerRegistered;
    result.notifications = [...spot.notifications];
  });
  await server.spots.remove(created.spotRid);

  return {
    match: match.toString(),
    winner: result.winner,
    firstPacket: server.channelEvents[0]?.packetName,
    timerRegistered: result.timerRegistered,
    notifications: result.notifications
  };
}

function getProvider(module, token) {
  const provider = module.providers.find((entry) => entry.provide === token);
  if (provider === undefined || provider.useValue === undefined) {
    throw new Error(`Sample provider is not registered: ${String(token)}`);
  }
  return provider.useValue;
}

module.exports = { createGameServer, playDeterministicGame };
