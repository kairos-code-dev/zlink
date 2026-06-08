const { BingoPlayerClient } = require('./bingo-player-client');
const { SampleNames, SampleTimings } = require('../Shared/Configuration/sample-names');
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  SubmitBingoCardRes
} from '../Shared/Contracts/messages';

type BingoClientOptions = {
  sessionEndpoint: string;
};

type BingoRoomState = {
  status: string;
  hostActorId: string | null;
  winners: string[];
  players: Array<{
    actorId: string;
    card: number[];
    marks: boolean[];
  }>;
};

type BingoSampleResult = {
  authentications: AuthenticateSessionRes[];
  matches: MatchBingoRes[];
  submissions: SubmitBingoCardRes[];
  started: BingoRoomState;
  ended: BingoRoomState;
  playerJoinedPushCounts: number[];
  startedPushCounts: number[];
  drawnPushCounts: number[];
  endedPushCounts: number[];
};

class BingoClientApp {
  [key: string]: any;
  async run(options: BingoClientOptions): Promise<BingoSampleResult> {
    const clients = SampleNames.actorIds.map((actorId) => new BingoPlayerClient(actorId, options.sessionEndpoint));
    try {
      await Promise.all(clients.map((client) => client.connect()));

      const authentications = [
        await clients[0].authenticate(),
        await clients[1].authenticate()
      ];
      requireCondition(authentications[0].actorId === 'player-1', 'First client must authenticate as player-1.');
      requireCondition(authentications[1].actorId === 'player-2', 'Second client must authenticate as player-2.');

      const firstMatch = await clients[0].match();
      requireCondition(firstMatch.state.status === 'WaitingForPlayers', 'First match must create a waiting room.');

      const playerJoinedForFirst = waitForNotify(clients[0], () => clients[0].notifications.playerJoined.length >= 2);
      const gameStartedForBoth = Promise.all([
        waitForNotify(clients[0], () => clients[0].notifications.started.length >= 1),
        waitForNotify(clients[1], () => clients[1].notifications.started.length >= 1)
      ]);
      const secondMatch = await clients[1].match();
      requireCondition(secondMatch.roomId === firstMatch.roomId, 'Both players must join the same room.');
      requireCondition(secondMatch.state.status === 'Running', 'Second match must automatically start the room.');
      await Promise.all([playerJoinedForFirst, gameStartedForBoth]);

      const endedForBoth = Promise.all([
        waitForNotify(clients[0], () => clients[0].notifications.ended.length >= 1),
        waitForNotify(clients[1], () => clients[1].notifications.ended.length >= 1)
      ]);
      const submissions = [
        await clients[0].submitCard(firstMatch.roomId),
        await clients[1].submitCard(firstMatch.roomId)
      ];
      requireCondition(submissions[0].state.status === 'Running', 'First card submit keeps the room running.');
      requireCondition(submissions[1].state.players.every((player) => player.card.length === 9), 'Both submitted cards must be visible in state.');
      await endedForBoth;

      const result = {
        authentications,
        matches: [firstMatch, secondMatch],
        submissions,
        started: clients[0].notifications.started.at(-1).state,
        ended: clients[0].notifications.ended.at(-1).state,
        playerJoinedPushCounts: clients.map((client) => client.notifications.playerJoined.length),
        startedPushCounts: clients.map((client) => client.notifications.started.length),
        drawnPushCounts: clients.map((client) => client.notifications.drawn.length),
        endedPushCounts: clients.map((client) => client.notifications.ended.length)
      };
      validate(result);
      return result;
    } finally {
      await Promise.allSettled(clients.map((client) => client.close()));
    }
  }
}

async function waitForNotify(client: any, predicate: () => boolean): Promise<void> {
  const deadline = Date.now() + SampleTimings.requestTimeout;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error(`Timed out waiting for Bingo notify for ${client.actorId}.`);
}

function validate(result: BingoSampleResult): void {
  requireCondition(new Set(result.authentications.map((auth) => auth.actorId)).size === 2, 'Two clients must authenticate as distinct actors.');
  requireCondition(new Set(result.matches.map((match) => match.roomId)).size === 1, 'Both match requests must return the same room.');
  requireCondition(result.started.status === 'Running', 'Room must start automatically after the second join.');
  requireCondition(result.ended.status === 'Finished', 'Room must finish through server timer draws.');
  requireCondition(result.ended.winners.length > 0, 'The deterministic sample must produce a winner.');
  requireCondition(result.ended.players.every((player) => player.card.length === 9), 'Each player card must contain 9 cells.');
  requireCondition(result.ended.players.every((player) => player.marks[4]), 'Center free cell must start marked.');
  requireCondition(result.startedPushCounts.every((count) => count > 0), 'Each client must receive game-start push.');
  requireCondition(result.drawnPushCounts.every((count) => count > 0), 'Each client must receive draw push.');
  requireCondition(result.endedPushCounts.every((count) => count > 0), 'Each client must receive game-ended push.');
}

function requireCondition(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
}

export { BingoClientApp };
