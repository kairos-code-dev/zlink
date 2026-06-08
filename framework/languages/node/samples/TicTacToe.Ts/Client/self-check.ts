const assert = require('node:assert/strict');
const path = require('node:path');
const connector = require('../../../../packages/stream-connector/dist');
const json = require('../../../../packages/stream-connector-json/dist');
const nestjs = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint, withServers } = require('./sample-process-host');
const {
  PacketNames,
  SampleNames,
  SampleTimings,
  authenticateReq,
  createGameReq,
  joinGameReq,
  placeMarkStreamReq
} = require('../Shared/Contracts/messages');
import type {
  CreateGameHttpRes
} from '../Shared/Contracts/messages';

type NestjsPackage = typeof nestjs;

type StreamNotification = {
  packetName: string;
  payload: any;
};

type StreamClient = {
  notifications: StreamNotification[];
  connect(): Promise<void>;
  close(): Promise<void>;
};

async function main(): Promise<void> {
  const playEndpoint = await reserveTcpEndpoint();
  const playStreamEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  const apiHttpEndpoint = toHttpEndpoint(await reserveTcpEndpoint());
  assertNestModule(nestjs.zlinkFramework()
    .options({
      streams: {
        [SampleNames.clientStreamNode]: { bind: playStreamEndpoint, session: class SmokeSession {} }
      }
    })
    .clientServerChannel(SampleNames.apiChannel, (channel) => channel
      .client(apiEndpoint)
      .server(apiEndpoint))
    .clientServerChannel(SampleNames.playChannel, (channel) => channel
      .client(playEndpoint)
      .server(playEndpoint))
    .build(), nestjs);

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
  ], async (): Promise<void> => {
    const game = await createGame(apiHttpEndpoint, 'match-ready');
    const x = createPlayerClient(playStreamEndpoint);
    const o = createPlayerClient(playStreamEndpoint);
    try {
      await x.connect();
      await o.connect();
      const xAuth = await requestJson(x, PacketNames.authenticateReq, authenticateReq('p1'));
      const oAuth = await requestJson(o, PacketNames.authenticateReq, authenticateReq('p2'));
      const xSelfJoined = waitForNoNotify(x, PacketNames.playerJoinedNotify);
      const xOpponentJoined = waitForNotify(x, PacketNames.playerJoinedNotify, (payload) => payload.actorId === 'p2');
      const xRunningState = waitForNotify(x, PacketNames.gameStateNotify, (payload) => payload.state.status === 'InProgress');
      const xJoin = await requestJson(x, PacketNames.joinGameReq, joinGameReq(game.roomId));
      await xSelfJoined;
      const oJoin = await requestJson(o, PacketNames.joinGameReq, joinGameReq(game.roomId));
      await Promise.all([xOpponentJoined, xRunningState]);
      const oFinalState = waitForNotify(o, PacketNames.gameStateNotify, (payload) => payload.state.winner === 'p1');
      const moves = [
        await requestJson(x, PacketNames.placeMarkReq, placeMarkStreamReq(0)),
        await requestJson(o, PacketNames.placeMarkReq, placeMarkStreamReq(3)),
        await requestJson(x, PacketNames.placeMarkReq, placeMarkStreamReq(1)),
        await requestJson(o, PacketNames.placeMarkReq, placeMarkStreamReq(4)),
        await requestJson(x, PacketNames.placeMarkReq, placeMarkStreamReq(2))
      ];
      await oFinalState;

      assert.equal(game.gameName, 'match-ready');
      assert.equal(game.playEndpoint, playStreamEndpoint);
      assert.deepEqual([xAuth.actorId, oAuth.actorId], ['p1', 'p2']);
      assert.deepEqual([xJoin.mark, oJoin.mark], ['X', 'O']);
      assert.equal(moves.at(-1).state.board, 'XXXOO....');
      assert.equal(moves.at(-1).state.status, 'Won');
      assert.equal(moves.at(-1).state.winner, 'p1');
      assert.equal(xJoin.state.status, 'WaitingForPlayers');
      assert.equal(oJoin.state.status, 'InProgress');
      assert.equal(x.notifications.filter((item) => item.packetName === PacketNames.playerJoinedNotify).length, 1);
      assert.equal(o.notifications.some((item) => item.packetName === PacketNames.playerJoinedNotify), false);
      assert.equal(x.notifications.at(-1).packetName, PacketNames.gameStateNotify);
      assert.equal(o.notifications.at(-1).payload.state.winner, 'p1');
    } finally {
      await Promise.allSettled([x.close(), o.close()]);
    }
  });

  console.log('PASS TicTacToe.Ts');
}

function assertNestModule(options: any, zlinkNestjs: NestjsPackage = nestjs): any {
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

function createPlayerClient(endpoint: string): StreamClient {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  client.notifications = [];
  for (const packetName of [PacketNames.playerJoinedNotify, PacketNames.gameStateNotify]) {
    json.onJson(client, packetName, (message: { payload: unknown }) => {
      client.notifications.push({ packetName, payload: message.payload });
    });
  }
  return client;
}

async function requestJson<TResponse = any>(client: StreamClient, packetName: string, payload: unknown): Promise<TResponse> {
  return await json
    .requestJson(client, payload)
    .packetName(packetName)
    .timeout(SampleTimings.requestTimeout)
    .submit();
}

async function createGame(apiHttpEndpoint: string, gameName: string): Promise<CreateGameHttpRes> {
  const response = await fetch(`${apiHttpEndpoint}/games`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(createGameReq(gameName))
  });
  if (!response.ok) {
    throw new Error(`Create game failed: ${response.status} ${await response.text()}`);
  }
  return await response.json();
}

function toHttpEndpoint(tcpEndpoint: string): string {
  return tcpEndpoint.replace('tcp://', 'http://');
}

async function waitForNotify(client: StreamClient, packetName: string, predicate: (payload: any) => boolean): Promise<void> {
  await waitFor(() => client.notifications.some((item) => item.packetName === packetName && predicate(item.payload)));
}

async function waitForNoNotify(client: StreamClient, packetName: string): Promise<void> {
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(client.notifications.some((item) => item.packetName === packetName), false);
}

async function waitFor(predicate: () => boolean): Promise<void> {
  const deadline = Date.now() + 2000;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error('Timed out waiting for stream notification.');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
