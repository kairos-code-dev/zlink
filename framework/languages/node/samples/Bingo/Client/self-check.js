const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../packages/nestjs/dist');
const { createChannelClient } = require('../../shared/channel-runtime');
const { assertNestModule } = require('../../shared/nestjs-smoke');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');

async function main() {
  const registryEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  assertNestModule({
    channels: {
      'bingo.api': { client: { manualConnections: [apiEndpoint] } },
      'bingo.play': { client: { manualConnections: [playEndpoint] }, server: { bind: playEndpoint } }
    }
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
    const client = await createChannelClient({
      channelName: 'bingo.api',
      peers: [apiEndpoint]
    });
    try {
      const auth = await client.request('AuthenticatePlayerReq', { accessToken: 'player-1' }, 7000);
      assert.equal(auth.accepted, true);
      assert.equal(auth.actorId, 'player-1');
      assert.equal(auth.displayName, 'Player 1');

      const first = await client.request('MatchBingoApiReq', {
        actorId: auth.actorId,
        displayName: auth.displayName,
        mode: 'four-player'
      }, 7000);
      const second = await client.request('MatchBingoApiReq', {
        actorId: 'player-2',
        displayName: 'Player 2',
        mode: 'four-player'
      }, 7000);
      assert.equal(first.roomId, 'bingo-room-001');
      assert.equal(second.roomId, 'bingo-room-001');
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
