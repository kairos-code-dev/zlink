const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../packages/nestjs/dist');
const { createChannelClient } = require('../../shared/channel-runtime');
const { assertNestModule } = require('../../shared/nestjs-smoke');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');

async function main() {
  const playEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  assertNestModule({
    channels: {
      api: { client: { manualConnections: [apiEndpoint] } },
      play: { client: { manualConnections: [playEndpoint] }, server: { bind: playEndpoint } }
    }
  }, nestjs);

  await withServers([
    {
      entry: path.resolve(__dirname, '../Server/Play/main.js'),
      env: { TICTACTOE_PLAY_ENDPOINT: playEndpoint }
    },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        TICTACTOE_API_ENDPOINT: apiEndpoint,
        TICTACTOE_PLAY_ENDPOINT: playEndpoint
      }
    }
  ], async () => {
    const client = await createChannelClient({
      channelName: 'api',
      peers: [apiEndpoint]
    });
    try {
      const result = await client.request('RunTicTacToe', { matchId: 'match-ready' }, 7000);
      assert.equal(result.match, 'match-ready');
      assert.equal(result.board, 'XXXOO....');
      assert.equal(result.winner, 'p1');
      assert.equal(result.firstPacket, 'CreateMatch');
      assert.equal(result.timerRegistered, true);
      assert.equal(result.notifications.at(0).packetName, 'PlayerJoinedNotify');
      assert.equal(result.notifications.at(-1).packetName, 'GameStateNotify');
      assert.equal(result.notifications.at(-1).winner, 'p1');
    } finally {
      await client.stop();
    }
  });

  console.log('PASS TicTacToe');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
