const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../packages/nestjs/dist');
const { createRouteClient } = require('../../shared/route-runtime');
const { assertNestModule } = require('../../shared/nestjs-smoke');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');
const { SampleNames, SampleTimings } = require('../Shared/Configuration/sample-names');
const { PacketNames } = require('../Shared/Contracts/messages');

async function main() {
  const registryEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const apiClientEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const sessionApiClientEndpoint = await reserveTcpEndpoint();
  const sessionPlayClientEndpoint = await reserveTcpEndpoint();
  const p1Endpoint = await reserveTcpEndpoint();
  const p2Endpoint = await reserveTcpEndpoint();
  assertNestModule({
    routeChannels: [{
      routerChannelId: SampleNames.routeChannel,
      bind: p1Endpoint,
      routingId: 'sample-client',
      manualConnections: [sessionEndpoint]
    }]
  }, nestjs);

  await withServers([
    { entry: path.resolve(__dirname, '../Server/Registry/main.js'), env: { TICTACTOE_SG_REGISTRY_ENDPOINT: registryEndpoint } },
    {
      entry: path.resolve(__dirname, '../Server/Play/main.js'),
      env: { TICTACTOE_SG_PLAY_ENDPOINT: playEndpoint }
    },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        TICTACTOE_SG_API_ENDPOINT: apiEndpoint,
        TICTACTOE_SG_API_CLIENT_ENDPOINT: apiClientEndpoint,
        TICTACTOE_SG_PLAY_ENDPOINT: playEndpoint
      }
    },
    {
      entry: path.resolve(__dirname, '../Server/Session/main.js'),
      env: {
        TICTACTOE_SG_SESSION_ENDPOINT: sessionEndpoint,
        TICTACTOE_SG_SESSION_API_CLIENT_ENDPOINT: sessionApiClientEndpoint,
        TICTACTOE_SG_SESSION_PLAY_CLIENT_ENDPOINT: sessionPlayClientEndpoint,
        TICTACTOE_SG_API_ENDPOINT: apiEndpoint,
        TICTACTOE_SG_PLAY_ENDPOINT: playEndpoint
      }
    }
  ], async () => {
    const p1 = await createSessionClient('p1-client', p1Endpoint, sessionEndpoint);
    const p2 = await createSessionClient('p2-client', p2Endpoint, sessionEndpoint);
    try {
      const p1Auth = await p1.request(PacketNames.authenticateSessionReq, { accessToken: 'p1' });
      const p2Auth = await p2.request(PacketNames.authenticateSessionReq, { accessToken: 'p2' });
      const match = await p1.request(PacketNames.createMatchSessionReq, { matchName: 'match' });
      const p2Join = await p2.request(PacketNames.joinMatchReq, { matchId: match.matchId });
      const moves = [
        await p1.request(PacketNames.placeMarkSessionReq, { matchId: match.matchId, cell: 0 }),
        await p2.request(PacketNames.placeMarkSessionReq, { matchId: match.matchId, cell: 3 }),
        await p1.request(PacketNames.placeMarkSessionReq, { matchId: match.matchId, cell: 1 }),
        await p2.request(PacketNames.placeMarkSessionReq, { matchId: match.matchId, cell: 4 }),
        await p1.request(PacketNames.placeMarkSessionReq, { matchId: match.matchId, cell: 2 })
      ];
      const p1Push = await p1.request(PacketNames.notificationsReq, { afterSeq: 0 });
      const p2Push = await p2.request(PacketNames.notificationsReq, { afterSeq: 0 });
      const delivered = [...p1Push.delivered, ...p2Push.delivered];
      const pushedByPacket = delivered.reduce((counts, entry) => {
        counts[entry.packetName] = (counts[entry.packetName] ?? 0) + 1;
        return counts;
      }, {});

      assert.deepEqual([p1Auth.actorId, p2Auth.actorId], ['p1', 'p2']);
      assert.equal(match.matchName, 'match');
      assert.equal(match.join.state.status, 'WaitingForPlayers');
      assert.equal(p2Join.state.status, 'Running');
      assert.equal(moves.at(-1).state.board, 'XXXOO....');
      assert.equal(moves.at(-1).state.winnerActorId, 'p1');
      assert.equal(pushedByPacket.OpponentJoinedNotify, 2);
      assert.equal(pushedByPacket.TurnChangedNotify, 10);
      assert.equal(pushedByPacket.GameEndedNotify, 2);
    } finally {
      await Promise.allSettled([p1.stop(), p2.stop()]);
    }
  });

  console.log('PASS TicTacToe.SessionGateway');
}

async function createSessionClient(routingId, endpoint, sessionEndpoint) {
  const client = await createRouteClient({
    endpoint,
    routingId,
    peers: [sessionEndpoint]
  });
  return {
    request(packetName, payload) {
      return client.request(SampleNames.sessionNode, packetName, payload, SampleTimings.requestTimeout);
    },
    stop() {
      return client.stop();
    }
  };
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
