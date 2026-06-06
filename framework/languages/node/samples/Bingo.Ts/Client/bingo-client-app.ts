require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_ROUTE_CLIENT } = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint } = require('./sample-process-host');
const { BingoPlayerClient } = require('./bingo-player-client');
const { SampleNames, SampleTimings } = require('../Shared/Configuration/sample-names');

class BingoClientApp {
  [key: string]: any;
  async run(options) {
    const routeClients = [];

    try {
      const clients = [];
      for (const actorId of SampleNames.actorIds) {
        const routeClient = await createRouteClient({
          endpoint: await reserveTcpEndpoint(),
          routingId: `bingo-client-${actorId}`,
          peers: [options.sessionEndpoint]
        });
        routeClients.push(routeClient);
        clients.push(new BingoPlayerClient(actorId, routeClient));
      }
      const authentications = [];
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

async function isRejected(action) {
  try {
    await action();
    return false;
  } catch {
    return true;
  }
}

async function waitForEnded(clients) {
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

function validate(result) {
  requireCondition(new Set(result.authentications.map((auth) => auth.actorId)).size === 4, 'Four clients must authenticate as distinct actors.');
  requireCondition(new Set(result.matches.map((match) => match.roomId)).size === 1, 'All match requests must return the same room.');
  requireCondition(result.matches[0].state.hostActorId === result.authentications[0].actorId, 'First joined actor must become host.');
  requireCondition(result.earlyHostStartRejected, 'Host start must be rejected before four players join.');
  requireCondition(result.nonHostStartRejected, 'Non-host start must be rejected.');
  requireCondition(result.started.state.status === 'Running', 'Host start must put room into Running status.');
  requireCondition(result.ended.status === 'Finished', 'Room must finish through timer draws.');
  requireCondition(result.ended.winners.length > 1, 'The deterministic sample must include same-sequence winners.');
  requireCondition(result.ended.players.every((player) => player.card.length === 25), 'Each player card must contain 25 cells.');
  requireCondition(result.ended.players.every((player) => player.marks[12]), 'Center free cell must start marked.');
  requireCondition(result.startedPushCounts.every((count) => count > 0), 'Each client must receive game-start push.');
  requireCondition(result.drawnPushCounts.every((count) => count > 0), 'Each client must receive draw push.');
  requireCondition(result.endedPushCounts.every((count) => count > 0), 'Each client must receive game-ended push.');
}

function requireCondition(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

export { BingoClientApp };

async function createRouteClient({ endpoint, routingId, peers }) {
  class BingoClientRouteModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          'sample-route': {
            bind: endpoint,
            routingId,
            manualConnections: peers
          }
        }
      })
    ]
  })(BingoClientRouteModule);

  const app = await NestFactory.createApplicationContext(BingoClientRouteModule, {
    logger: false,
    abortOnError: false
  });
  const routeClient = app.get(ZLINK_ROUTE_CLIENT, { strict: false });

  return {
    async request(targetNodeRid, packetName, payload, timeoutMs = 1000) {
      return await retry(() => routeClient
        .request('sample-route', targetNodeRid, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit(), { maxAttempts: 100 });
    },
    async stop() {
      await closeNestRuntime(app);
    }
  };
}

async function retry(action, options: any = {}) {
  const maxAttempts = options.maxAttempts ?? 5;
  let lastError;
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

async function closeNestRuntime(container) {
  try {
    await container.close();
  } catch (error) {
    if (error?.name === 'CloseError' && (error?.code === 0 || error?.code === 401)) {
      return;
    }
    throw error;
  }
}
