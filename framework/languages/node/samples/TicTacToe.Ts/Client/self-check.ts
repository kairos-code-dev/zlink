const assert = require('node:assert/strict');
const path = require('node:path');
const connector = require('../../../../packages/stream-connector/dist');
const json = require('../../../../packages/stream-connector-json/dist');
const nestjs = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint, withServers } = require('./sample-process-host');
const { PacketNames, SampleNames, SampleTimings } = require('../Shared/Contracts/messages');

async function main() {
  const playEndpoint = await reserveTcpEndpoint();
  const playStreamEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const apiHttpEndpoint = toHttpEndpoint(await reserveTcpEndpoint());
  assertNestModule({
    clientServerChannels: {
      [SampleNames.apiChannel]: { client: { manualConnections: [apiEndpoint] }, server: { bind: apiEndpoint } },
      [SampleNames.playChannel]: { client: { manualConnections: [playEndpoint] }, server: { bind: playEndpoint } }
    },
    streams: {
      [SampleNames.clientStreamNode]: { bind: playStreamEndpoint, session: class SmokeSession {} }
    }
  }, nestjs);

  await withServers([
    {
      entry: path.resolve(__dirname, '../Server/Play/main.js'),
      env: {
        TICTACTOE_API_ENDPOINT: apiEndpoint,
        TICTACTOE_PLAY_ENDPOINT: playEndpoint,
        TICTACTOE_PLAY_STREAM_ENDPOINT: playStreamEndpoint
      }
    },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        TICTACTOE_API_ENDPOINT: apiEndpoint,
        TICTACTOE_API_HTTP_ENDPOINT: apiHttpEndpoint,
        TICTACTOE_PLAY_ENDPOINT: playEndpoint
      }
    }
  ], async () => {
    const game = await createGame(apiHttpEndpoint, 'match-ready');
    const x = createPlayerClient(playStreamEndpoint);
    const o = createPlayerClient(playStreamEndpoint);
    try {
      await x.connect();
      await o.connect();
      const xAuth = await requestJson(x, PacketNames.authenticateReq, { accessToken: 'p1' });
      const oAuth = await requestJson(o, PacketNames.authenticateReq, { accessToken: 'p2' });
      const xJoin = await requestJson(x, PacketNames.joinGameReq, { gameId: game.gameId });
      const oJoin = await requestJson(o, PacketNames.joinGameReq, { gameId: game.gameId });
      const moves = [
        await requestJson(x, PacketNames.placeMarkReq, { gameId: game.gameId, cell: 0 }),
        await requestJson(o, PacketNames.placeMarkReq, { gameId: game.gameId, cell: 3 }),
        await requestJson(x, PacketNames.placeMarkReq, { gameId: game.gameId, cell: 1 }),
        await requestJson(o, PacketNames.placeMarkReq, { gameId: game.gameId, cell: 4 }),
        await requestJson(x, PacketNames.placeMarkReq, { gameId: game.gameId, cell: 2 })
      ];

      assert.equal(game.gameName, 'match-ready');
      assert.equal(game.playEndpoint, playStreamEndpoint);
      assert.deepEqual([xAuth.actorId, oAuth.actorId], ['p1', 'p2']);
      assert.deepEqual([xJoin.mark, oJoin.mark], ['X', 'O']);
      assert.equal(moves.at(-1).state.board, 'XXXOO....');
      assert.equal(moves.at(-1).state.status, 'Won');
      assert.equal(moves.at(-1).state.winner, 'p1');
      await waitFor(() => o.notifications.some((item) =>
        item.packetName === PacketNames.gameStateNotify && item.payload.winner === 'p1'));
      assert.equal(x.notifications.some((item) => item.packetName === PacketNames.playerJoinedNotify), true);
      assert.equal(o.notifications.some((item) => item.packetName === PacketNames.playerJoinedNotify), true);
      assert.equal(x.notifications.at(-1).packetName, PacketNames.gameStateNotify);
      assert.equal(o.notifications.at(-1).payload.winner, 'p1');
    } finally {
      await Promise.allSettled([x.close(), o.close()]);
    }
  });

  console.log('PASS TicTacToe.Ts');
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

function createPlayerClient(endpoint) {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  client.notifications = [];
  for (const packetName of [PacketNames.playerJoinedNotify, PacketNames.gameStateNotify]) {
    json.onJson(client, packetName, (message) => {
      client.notifications.push({ packetName, payload: message.payload });
    });
  }
  return client;
}

async function requestJson(client, packetName, payload) {
  return await json
    .requestJson(client, payload)
    .packetName(packetName)
    .timeout(SampleTimings.requestTimeout)
    .submit();
}

async function createGame(apiHttpEndpoint, gameName) {
  const response = await fetch(`${apiHttpEndpoint}/games`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ gameName })
  });
  if (!response.ok) {
    throw new Error(`Create game failed: ${response.status} ${await response.text()}`);
  }
  return await response.json();
}

function toHttpEndpoint(tcpEndpoint) {
  return tcpEndpoint.replace('tcp://', 'http://');
}

async function waitFor(predicate) {
  const deadline = Date.now() + 2000;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error('Timed out waiting for stream notification.');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
