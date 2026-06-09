const path = require('node:path');
const nestjs = require('../../../../packages/nestjs/dist');
const connector = require('../../../../packages/stream-connector/dist');
const json = require('../../../../packages/stream-connector-json/dist');
const { reserveTcpEndpoint, withServers } = require('./sample-process-host');
import { BingoClientApp } from './bingo-client-app';
import type { ZlinkStreamConnector } from '../../../packages/stream-connector/dist';
const { SampleTimings } = require('../Shared/Configuration/sample-names');

type NestjsPackage = typeof nestjs;

async function main(): Promise<void> {
  const registryEndpoint = await reserveTcpEndpoint();
  const sessionEndpoint = await reserveTcpEndpoint();
  const playEndpoint = await reserveTcpEndpoint();
  const notificationEndpoint = await reserveTcpEndpoint();
  const apiEndpoint = await reserveTcpEndpoint();
  assertNestModule(nestjs.zlinkFramework()
    .clientServerChannel('bingo.registry', (channel) => channel
      .client(registryEndpoint))
    .build(), nestjs);

  await withServers([
    { entry: path.resolve(__dirname, '../Server/Registry/main.js'), env: { BINGO_REGISTRY_ENDPOINT: registryEndpoint } },
    {
      entry: path.resolve(__dirname, '../Server/Play/main.js'),
      env: {
        BINGO_REGISTRY_ENDPOINT: registryEndpoint,
        BINGO_PLAY_ENDPOINT: playEndpoint,
        BINGO_NOTIFICATION_ENDPOINT: notificationEndpoint
      }
    },
    {
      entry: path.resolve(__dirname, '../Server/Api/main.js'),
      env: {
        BINGO_REGISTRY_ENDPOINT: registryEndpoint,
        BINGO_API_ENDPOINT: apiEndpoint
      }
    },
    {
      entry: path.resolve(__dirname, '../Server/Session/main.js'),
      env: {
        BINGO_REGISTRY_ENDPOINT: registryEndpoint,
        BINGO_SESSION_ENDPOINT: sessionEndpoint
      }
    }
  ], async (): Promise<void> => {
    const client1 = createClient(sessionEndpoint);
    const client2 = createClient(sessionEndpoint);
    try {
      await new BingoClientApp().run(client1, client2);
    } finally {
      await Promise.allSettled([
        client1.close(),
        client2.close()
      ]);
    }
  });

  console.log('PASS Bingo.Ts');
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

function createClient(sessionEndpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    codec: json.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
