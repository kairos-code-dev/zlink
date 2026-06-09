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
  AuthenticateRes,
  CreateGameHttpRes,
  JoinGameRes,
  PlaceMarkRes
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector } from '../../../packages/stream-connector/dist';

type NestjsPackage = typeof nestjs;

type StreamNotification = {
  packetName: string;
  payload: any;
};

type StreamClient = ZlinkStreamConnector & {
  notifications: StreamNotification[];
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
      const xAuth = await x.request(authenticateReq('p1')).submit<AuthenticateRes>();
      const oAuth = await o.request(authenticateReq('p2')).submit<AuthenticateRes>();
      const xSelfJoined = waitForNoNotify(x, PacketNames.playerJoinedNotify);
      const xOpponentJoined = x.waitFor<any>(PacketNames.playerJoinedNotify).where((message) => message.payload.actorId === 'p2').submit();
      const xRunningState = x.waitFor<any>(PacketNames.gameStateNotify).where((message) => message.payload.state.status === 'InProgress').submit();
      const xJoin = await x.request(joinGameReq(game.roomId)).submit<JoinGameRes>();
      await xSelfJoined;
      const oJoin = await o.request(joinGameReq(game.roomId)).submit<JoinGameRes>();
      const [joinedMessage, runningMessage] = await Promise.all([xOpponentJoined, xRunningState]);
      const joined = joinedMessage.payload;
      const running = runningMessage.payload;
      assert.equal(joined.actorId, 'p2');
      assert.equal(joined.mark, 'O');
      assert.equal(joined.roomId, game.roomId);
      assert.equal(joined.state.status, 'InProgress');
      assert.equal(running.state.status, 'InProgress');
      assert.equal(running.state.nextTurn, 'p1');
      const oFinalState = o.waitFor<any>(PacketNames.gameStateNotify).where((message) => message.payload.state.winner === 'p1').submit();
      const moves = [
        await x.request(placeMarkStreamReq(0)).submit<PlaceMarkRes>(),
        await o.request(placeMarkStreamReq(3)).submit<PlaceMarkRes>(),
        await x.request(placeMarkStreamReq(1)).submit<PlaceMarkRes>(),
        await o.request(placeMarkStreamReq(4)).submit<PlaceMarkRes>(),
        await x.request(placeMarkStreamReq(2)).submit<PlaceMarkRes>()
      ];
      const finalState = (await oFinalState).payload;
      const lastMoveState = moves[moves.length - 1].state as any;
      const xJoinState = xJoin.state as any;
      const oJoinState = oJoin.state as any;

      assert.equal(game.gameName, 'match-ready');
      assert.equal(game.playEndpoint, playStreamEndpoint);
      assert.deepEqual([xAuth.actorId, oAuth.actorId], ['p1', 'p2']);
      assert.deepEqual([xJoin.mark, oJoin.mark], ['X', 'O']);
      assert.equal(lastMoveState.board, 'XXXOO....');
      assert.equal(lastMoveState.status, 'Won');
      assert.equal(lastMoveState.winner, 'p1');
      assert.equal(xJoinState.status, 'WaitingForPlayers');
      assert.equal(oJoinState.status, 'InProgress');
      assert.equal(x.notifications.filter((item) => item.packetName === PacketNames.playerJoinedNotify).length, 1);
      assert.equal(o.notifications.some((item) => item.packetName === PacketNames.playerJoinedNotify), false);
      assert.equal(x.notifications.at(-1).packetName, PacketNames.gameStateNotify);
      assert.equal(finalState.state.winner, 'p1');
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
    codec: json.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  }) as StreamClient;
  client.notifications = [];
  for (const packetName of [PacketNames.playerJoinedNotify, PacketNames.gameStateNotify]) {
    client.on(packetName, (message: { payload: unknown }) => {
      client.notifications.push({ packetName, payload: message.payload });
    });
  }
  return client;
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

async function waitForNoNotify(client: StreamClient, packetName: string): Promise<void> {
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(client.notifications.some((item) => item.packetName === packetName), false);
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
