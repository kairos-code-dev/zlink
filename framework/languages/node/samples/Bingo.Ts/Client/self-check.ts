const assert = require('node:assert/strict');
const path = require('node:path');
const nestjs = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint, withServers } = require('./sample-process-host');
import { BingoClientApp } from './bingo-client-app';

async function main() {
  const registryEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  assertNestModule({
    clientServerChannels: {
      'bingo.api': { client: { manualConnections: [apiEndpoint] } },
      'bingo.play': { client: { manualConnections: [playEndpoint] }, server: { bind: playEndpoint } }
    }
  }, nestjs);

  await withServers([
    { entry: path.resolve(__dirname, '../Server/Registry/main.js'), env: { BINGO_REGISTRY_ENDPOINT: registryEndpoint } },
    {
      entry: path.resolve(__dirname, '../Server/Session/main.js'),
      env: {
        BINGO_SESSION_ENDPOINT: sessionEndpoint,
        BINGO_API_ENDPOINT: apiEndpoint,
        BINGO_PLAY_ENDPOINT: playEndpoint
      }
    },
    { entry: path.resolve(__dirname, '../Server/Play/main.js'), env: { BINGO_PLAY_ENDPOINT: playEndpoint } },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        BINGO_API_ENDPOINT: apiEndpoint,
        BINGO_PLAY_ENDPOINT: playEndpoint
      }
    }
  ], async () => {
    const result = await new BingoClientApp().run({ sessionEndpoint, playEndpoint });
    assert.equal(result.ended.status, 'Finished');
    assert.equal(result.ended.hostActorId, 'player-1');
    assert.deepEqual(result.ended.winners, ['player-1', 'player-3']);
    assert.equal(result.earlyHostStartRejected, true);
    assert.equal(result.nonHostStartRejected, true);
    assert.deepEqual(result.ended.players.map((player) => player.actorId), ['player-1', 'player-2', 'player-3', 'player-4']);
    assert.equal(result.startedPushCounts.length, 4);
    assert.equal(result.drawnPushCounts.length, 4);
    assert.equal(result.endedPushCounts.length, 4);
  });

  console.log('PASS Bingo.Ts');
}

function assertNestModule(options, zlinkNestjs = nestjs) {
  const module = zlinkNestjs.ZLinkModule.forRoot(options);
  const tokens = new Set(module.providers.map((provider) => provider.provide));
  for (const token of [
    zlinkNestjs.ZLINK_FRAMEWORK_RUNTIME,
    zlinkNestjs.ZLINK_CHANNEL_CLIENT,
    zlinkNestjs.ZLINK_ROUTE_CLIENT,
    zlinkNestjs.ZLINK_FANOUT_CLIENT
  ]) {
    if (!tokens.has(token)) {
      throw new Error(`NestJS ZLinkModule provider is missing: ${String(token)}`);
    }
  }
  return module;
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
