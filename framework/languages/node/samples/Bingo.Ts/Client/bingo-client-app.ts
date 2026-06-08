require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_ROUTE_CLIENT, zlinkFramework } = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint } = require('./sample-process-host');
const { BingoPlayerClient } = require('./bingo-player-client');
const { SampleNames, SampleTimings } = require('../Shared/Configuration/sample-names');
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  StateEnvelope
} from '../Shared/Contracts/messages';

type BingoClientOptions = {
  sessionEndpoint: string;
  playEndpoint?: string;
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

type BingoRouteClient = {
  request(targetNodeRid: string, packetName: string, payload: unknown, timeoutMs?: number): Promise<any>;
  stop(): Promise<void>;
};

type BingoSampleResult = {
  authentications: AuthenticateSessionRes[];
  matches: MatchBingoRes[];
  started: StateEnvelope;
  ended: BingoRoomState;
  playerJoinedPushCounts: number[];
  startedPushCounts: number[];
  drawnPushCounts: number[];
  endedPushCounts: number[];
  earlyHostStartRejected: boolean;
  nonHostStartRejected: boolean;
};

type RetryOptions = {
  maxAttempts?: number;
};

class BingoClientApp {
  [key: string]: any;
  async run(options: BingoClientOptions): Promise<BingoSampleResult> {
    const routeClients: BingoRouteClient[] = [];

    try {
      const clients: any[] = [];
      for (const actorId of SampleNames.actorIds) {
        const routeClient = await createRouteClient({
          endpoint: await reserveTcpEndpoint(),
          routingId: `bingo-client-${actorId}`,
          peers: [options.sessionEndpoint]
        });
        routeClients.push(routeClient);
        clients.push(new BingoPlayerClient(actorId, routeClient));
      }
      const authentications: AuthenticateSessionRes[] = [];
      for (const client of clients) {
        authentications.push(await client.authenticate());
      }

      const firstMatch = await clients[0].match();
      const earlyHostStartRejected = await isRejected(() => clients[0].start(firstMatch.roomId));
      const matches = [firstMatch];
      for (const client of clients.slice(1)) {
        matches.push(await client.match());
      }

      const nonHostStartRejected = await isRejected(() => clients[1].start(firstMatch.roomId));
      const started = await clients[0].start(firstMatch.roomId);
      const ended = await waitForEnded(clients);

      const result = {
        authentications,
        matches,
        started,
        ended,
        playerJoinedPushCounts: clients.map((client) => client.notifications.playerJoined.length),
        startedPushCounts: clients.map((client) => client.notifications.started.length),
        drawnPushCounts: clients.map((client) => client.notifications.drawn.length),
        endedPushCounts: clients.map((client) => client.notifications.ended.length),
        earlyHostStartRejected,
        nonHostStartRejected
      };
      validate(result);
      return result;
    } finally {
      for (const routeClient of routeClients.reverse()) {
        await routeClient.stop();
      }
    }
  }
}

async function isRejected(action: () => Promise<unknown>): Promise<boolean> {
  try {
    await action();
    return false;
  } catch {
    return true;
  }
}

async function waitForEnded(clients: any[]): Promise<BingoRoomState> {
  const deadline = Date.now() + SampleTimings.requestTimeout;
  while (Date.now() < deadline) {
    for (const client of clients) {
      await client.syncNotifications();
    }
    const ended = clients
      .flatMap((client) => client.notifications.ended)
      .at(-1);
    if (ended !== undefined) {
      return ended.state;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error('Timed out waiting for BingoGameEndedNotify.');
}

function validate(result: BingoSampleResult): void {
  requireCondition(new Set(result.authentications.map((auth) => auth.actorId)).size === 4, 'Four clients must authenticate as distinct actors.');
  requireCondition(new Set(result.matches.map((match) => match.roomId)).size === 1, 'All match requests must return the same room.');
  const firstMatchState = result.matches[0].state as BingoRoomState;
  const startedState = result.started.state as BingoRoomState;
  requireCondition(firstMatchState.hostActorId === result.authentications[0].actorId, 'First joined actor must become host.');
  requireCondition(result.earlyHostStartRejected, 'Host start must be rejected before four players join.');
  requireCondition(result.nonHostStartRejected, 'Non-host start must be rejected.');
  requireCondition(startedState.status === 'Running', 'Host start must put room into Running status.');
  requireCondition(result.ended.status === 'Finished', 'Room must finish through timer draws.');
  requireCondition(result.ended.winners.length > 1, 'The deterministic sample must include same-sequence winners.');
  requireCondition(result.ended.players.every((player) => player.card.length === 25), 'Each player card must contain 25 cells.');
  requireCondition(result.ended.players.every((player) => player.marks[12]), 'Center free cell must start marked.');
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

async function createRouteClient(
  { endpoint, routingId, peers }: { endpoint: string; routingId: string; peers: string[] }
): Promise<BingoRouteClient> {
  class BingoClientRouteModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .routerMesh('sample-route', (mesh) => mesh
            .bind(endpoint)
            .routingId(routingId)
            .connect(peers))
          .build()
      )
    ]
  })(BingoClientRouteModule);

  const app = await NestFactory.createApplicationContext(BingoClientRouteModule, {
    logger: false,
    abortOnError: false
  });
  const routeClient = app.get(ZLINK_ROUTE_CLIENT, { strict: false });

  return {
    async request(targetNodeRid: string, packetName: string, payload: unknown, timeoutMs: number = 1000): Promise<any> {
      return await retry(() => routeClient
        .request('sample-route', targetNodeRid, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit(), { maxAttempts: 100 });
    },
    async stop(): Promise<void> {
      await closeNestRuntime(app);
    }
  };
}

async function retry<TValue>(action: () => Promise<TValue>, options: RetryOptions = {}): Promise<TValue> {
  const maxAttempts = options.maxAttempts ?? 5;
  let lastError: unknown;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    try {
      return await action();
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

async function closeNestRuntime(container: { close(): Promise<void> }): Promise<void> {
  try {
    await container.close();
  } catch (error) {
    const candidate = error as { name?: string; code?: number };
    if (candidate.name === 'CloseError' && (candidate.code === 0 || candidate.code === 401)) {
      return;
    }
    throw error;
  }
}
