import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkSpotPublisherClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_SPOT_PUBLISHER_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import type { SpotPublishReq } from '../../Shared/messages';
import { SpotServiceNames } from '../../Shared/messages';
import { EvidenceStore } from '../Play/Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer, type HttpRoute } from '../Play/Support/http-server';

interface GatewayOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly spotRouterEndpoint: string;
  readonly spotPubEndpoint: string;
  readonly spotPubPeers: readonly string[];
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export async function startGatewayHost(args: readonly string[]): Promise<void> {
  const options = parseGatewayOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  evidence.add(`start|rid=${options.rid}`);
  let stopping = false;

  class GatewayModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addSpotMesh(SpotServiceNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .enablePubSub(options.spotPubEndpoint, undefined, options.spotPubPeers);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence }
    ]
  })(GatewayModule);

  const app = await NestFactory.createApplicationContext(GatewayModule, { logger: false, abortOnError: false });
  const publisher = app.get(ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) as ZLinkSpotPublisherClient;
  const server = await startHttpServer(options.httpUrl, createGatewayEndpoints(options, evidence, publisher, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createGatewayEndpoints(
  options: GatewayOptions,
  evidence: EvidenceStore,
  publisher: ZLinkSpotPublisherClient,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'gateway', rid: options.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/spot/publish',
      handle: async (body) => {
        const request = body as SpotPublishReq;
        await publisher
          .publishSpot(SpotServiceNames.spotChannel, SpotServiceNames.spotEventTopic, { marker: request.marker })
          .packetName('SpotMsg')
          .submit();
        evidence.add(`spot-publish|rid=${options.rid}|spot=${request.spotRid}|marker=${request.marker}`);
        return {
          operation: 'spot.sm-c4-publish',
          publisherRid: options.rid,
          spotRid: request.spotRid,
          marker: request.marker,
          evidence: evidence.snapshot()
        };
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

function parseGatewayOptions(args: readonly string[]): GatewayOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--')) {
      continue;
    }
    if (i + 1 >= args.length) {
      throw new Error(`Missing value for ${key}.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  const rid = required(values, 'rid');
  process.env.ZLINK_E2E_RID = rid;
  return {
    rid,
    httpUrl: required(values, 'http-url'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    spotPubEndpoint: required(values, 'spot-pub-endpoint'),
    spotPubPeers: splitList(values.get('spot-pub-peer')),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
}

function splitList(value: string | undefined): readonly string[] {
  if (value === undefined || value.trim().length === 0) {
    return [];
  }
  return value.split(',').map((part) => part.trim()).filter((part) => part.length > 0);
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
