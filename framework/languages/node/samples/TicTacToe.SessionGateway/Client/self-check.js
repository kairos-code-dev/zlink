const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../packages/nestjs/dist');
const { createRouteClient } = require('../../shared/route-runtime');
const { assertNestModule } = require('../../shared/nestjs-smoke');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');

async function main() {
  const registryEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const clientEndpoint = await reserveTcpEndpoint();
  assertNestModule({
    routeChannels: [{
      routerChannelId: 'sample-route',
      bind: clientEndpoint,
      routingId: 'sample-client',
      manualConnections: [sessionEndpoint]
    }]
  }, nestjs);

  await withServers([
    { entry: path.resolve(__dirname, '../Server/Registry/main.js'), env: { TICTACTOE_SG_REGISTRY_ENDPOINT: registryEndpoint } },
    { entry: path.resolve(__dirname, '../Server/Api/main.js'), env: { TICTACTOE_SG_API_ENDPOINT: apiEndpoint } },
    { entry: path.resolve(__dirname, '../Server/Play/main.js'), env: { TICTACTOE_SG_PLAY_ENDPOINT: playEndpoint } },
    { entry: path.resolve(__dirname, '../Server/Session/main.js'), env: { TICTACTOE_SG_SESSION_ENDPOINT: sessionEndpoint } }
  ], async () => {
    const client = await createRouteClient({
      endpoint: clientEndpoint,
      routingId: 'sample-client',
      peers: [sessionEndpoint]
    });
    try {
      const result = await client.request('session-server', 'RunSessionGateway', {});
      assert.equal(result.sameActor, true);
      assert.equal(result.finalState.board, 'XXXOO....');
      assert.equal(result.finalState.winnerActorId, 'p1');
      const pushedByPacket = result.delivered.reduce((counts, entry) => {
        counts[entry.packetName] = (counts[entry.packetName] ?? 0) + 1;
        return counts;
      }, {});
      assert.equal(pushedByPacket.OpponentJoinedNotify, 2);
      assert.equal(pushedByPacket.TurnChangedNotify, 10);
      assert.equal(pushedByPacket.GameEndedNotify, 2);
    } finally {
      await client.stop();
    }
  });

  console.log('PASS TicTacToe.SessionGateway');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
