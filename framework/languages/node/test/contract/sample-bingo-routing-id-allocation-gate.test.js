const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

function read(relativePath) {
  return fs.readFileSync(path.join(nodeRoot, relativePath), 'utf8');
}

test('Bingo roles declare the common automatic routing-id groups', () => {
  const api = read('samples/Bingo.Ts/Server/Api/bingo-api-module.ts');
  const play = read('samples/Bingo.Ts/Server/Play/bingo-play-module.ts');
  const session = read('samples/Bingo.Ts/Server/Session/bingo-session-module.ts');

  assert.match(api, /addClientServerChannel\(SampleNames\.apiChannel\)[\s\S]*useAllocatedRoutingId\(2, 'api'\)[\s\S]*setRoutingIdAllocationGroup\('bingo\.api'\)/);
  assert.match(play, /addClientServerChannel\(SampleNames\.playChannel\)[\s\S]*useAllocatedRoutingId\(2, 'play'\)[\s\S]*setRoutingIdAllocationGroup\('bingo\.play'\)/);
  assert.match(play, /addSpotMesh\(SampleNames\.roomSpotNode\)[\s\S]*useAllocatedRoutingId\(2, 'play'\)[\s\S]*setRoutingIdAllocationGroup\('bingo\.play'\)/);
  assert.match(session, /addSpotMesh\(SampleNames\.roomSpotNode\)[\s\S]*useAllocatedRoutingId\(2, 'session'\)[\s\S]*setRoutingIdAllocationGroup\('bingo\.session'\)/);
});

test('Bingo topology has no fixed or preferred routing-id configuration', () => {
  const config = read('samples/Bingo.Ts/Server/Configuration/sample-config.ts');
  const play = read('samples/Bingo.Ts/Server/Play/bingo-play-module.ts');
  const session = read('samples/Bingo.Ts/Server/Session/bingo-session-module.ts');
  const runner = read('samples/Bingo.Ts/Runner/sample-runner.mjs');

  for (const source of [config, play, session, runner]) {
    assert.doesNotMatch(source, /playSpotNodeRid|sessionSpotNodeRid|preferredPlayNodeRid/);
  }
  assert.doesNotMatch(`${play}\n${session}`, /\.routingId\(/);
});

test('Bingo session derives the preferred Play slot through the public allocation and spot APIs', () => {
  const handler = read('samples/Bingo.Ts/Server/Session/Sessions/Handlers/authenticate-session-handler.ts');
  assert.match(handler, /ZLINK_ALLOCATED_ROUTING_ID_PROVIDER/);
  assert.match(handler, /waitForReadyAllocation\('bingo\.session'/);
  assert.match(handler, /`play\$\{allocation\.slot\}`/);
  assert.match(handler, /resolveSpotHandle\(preferredPlayNodeRid/);
  assert.match(handler, /requestToSpot\(playEntrySpot, ensureRequest\)/);
  assert.doesNotMatch(handler, /listPeerLocations|requestToNode/);
});

test('Bingo Play channel preserves the preferred allocated owner through the Entry Spot', () => {
  const apiHandler = read('samples/Bingo.Ts/Server/Api/Handlers/match-bingo-handler.ts');
  const playHandler = read(
    'samples/Bingo.Ts/Server/Play/Infrastructure/ZLink/Handlers/allocate-bingo-room-handler.ts'
  );
  const client = read('samples/Bingo.Ts/Client/bingo-client-scenario.ts');

  assert.match(apiHandler, /requestToChannel\([\s\n]*SampleNames\.playChannel/);
  assert.match(playHandler, /requestToSpot\(preferredEntrySpot, new AllocateBingoRoomReq/);
  assert.match(playHandler, /request\.preferredOwnerNodeRid === localNodeRid/);
  assert.match(playHandler, /return new AllocateBingoRoomRes/);
  assert.match(client, /client1MatchRes\.roomOwnerNodeRid === client1Auth\.actorNodeRid/);
});

test('Bingo runner checks allocation evidence, start-order independence, and slot handoff', () => {
  const runner = read('samples/Bingo.Ts/Runner/sample-runner.mjs');
  assert.match(runner, /bingo routing allocation ready/);
  assert.match(runner, /WaitingForSlot/);
  assert.match(runner, /generation/);
  assert.match(runner, /replacement/);
});
