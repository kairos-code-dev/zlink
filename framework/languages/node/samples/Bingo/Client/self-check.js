const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../packages/nestjs/dist');
const { createRouteClient } = require('../../shared/route-runtime');
const { assertNestModule } = require('../../shared/nestjs-smoke');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');

async function main() {
  const registryEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const clientEndpoint = await reserveTcpEndpoint();
  assertNestModule({
    routeChannels: [{
      routerChannelId: 'sample-route',
      bind: clientEndpoint,
      routingId: 'sample-client',
      manualConnections: [apiEndpoint]
    }]
  }, nestjs);

  await withServers([
    { entry: path.resolve(__dirname, '../Server/Registry/main.js'), env: { BINGO_REGISTRY_ENDPOINT: registryEndpoint } },
    { entry: path.resolve(__dirname, '../Server/Session/main.js'), env: { BINGO_SESSION_ENDPOINT: sessionEndpoint } },
    { entry: path.resolve(__dirname, '../Server/Play/main.js'), env: { BINGO_PLAY_ENDPOINT: playEndpoint } },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        BINGO_API_ENDPOINT: apiEndpoint,
        BINGO_PLAY_ENDPOINT: playEndpoint
      }
    }
  ], async () => {
    const client = await createRouteClient({
      endpoint: clientEndpoint,
      routingId: 'sample-client',
      peers: [apiEndpoint]
    });
    try {
      const result = await client.request('api-server', 'RunBingo', {});
      assert.equal(result.room.status, 'Finished');
      assert.equal(result.room.hostActorId, 'p1');
      assert.deepEqual(result.room.winners, ['p1', 'p3']);
      assert.equal(result.earlyHostStartRejected, true);
      assert.equal(result.nonHostStartRejected, true);
      assert.deepEqual(result.room.players.map((player) => player.actorId), ['p1', 'p2', 'p3', 'p4']);
      assert.equal(result.notifications.filter((message) => message.packetName === 'BingoGameStarted').length, 4);
      assert.equal(result.notifications.filter((message) => message.packetName === 'BingoNumberDrawn').length, 4);
      assert.equal(result.notifications.filter((message) => message.packetName === 'BingoGameEnded').length, 4);
    } finally {
      await client.stop();
    }
  });

  console.log('PASS Bingo');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
