const path = require('node:path');
const connector = require('../../../../packages/stream-connector/dist');
const msgpack = require('../../../../packages/stream-connector-msgpack/dist');
const nestjs = require('../../../../packages/nestjs/dist');
const { reserveTcpEndpoint, withServers } = require('./sample-process-host');
import { TicTacToeClientApp } from './tictactoe-client-app';
const {
  SampleNames,
  SampleTimings
} = require('../Shared/Configuration/sample-settings');
import type { ZlinkStreamConnector } from '../../../packages/stream-connector/dist';

type NestjsPackage = typeof nestjs;

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
    const x = createPlayerClient(playStreamEndpoint);
    const o = createPlayerClient(playStreamEndpoint);
    try {
      await new TicTacToeClientApp().run(x, o, apiHttpEndpoint);
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

function createPlayerClient(endpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint,
    codec: msgpack.zlinkStreamMessagePackCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
}

function toHttpEndpoint(tcpEndpoint: string): string {
  return tcpEndpoint.replace('tcp://', 'http://');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
