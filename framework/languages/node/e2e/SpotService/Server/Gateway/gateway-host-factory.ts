import fs from 'node:fs';
import { Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type {
  ZLinkRuntimeEventHandler,
  ZLinkSpotEvent,
  ZLinkSpotPublisherClient
} from '@zlink-systems/framework';
import {
  ZLinkMessageFlowLogMode,
  ZLinkSpotEventKind,
  ZLinkSpotPeerKind,
  ZLinkSpotPeerState
} from '@zlink-systems/framework';
import {
  ZLINK_SPOT_PUBLISHER_CLIENT,
  ZLinkModule,
  zlinkFramework,
  zlinkRuntimeEventHandler
} from '@zlink-systems/nestjs';
import type { SpotPublishReq } from '../../Shared/messages';
import { SpotMsg, SpotServiceNames, spotServicePacket } from '../../Shared/messages';
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

  @Injectable()
  @zlinkRuntimeEventHandler()
  class GatewayPubSubReadiness implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
    private connected = false;

    async handle(event: ZLinkSpotEvent): Promise<void> {
      if (event.sourceName !== SpotServiceNames.spotChannel || event.event !== ZLinkSpotEventKind.PeersChanged) {
        return;
      }
      this.connected = event.peers.some((peer) =>
        peer.kind === ZLinkSpotPeerKind.SpotMesh && peer.state === ZLinkSpotPeerState.Connected);
    }

    requireConnected(): void {
      if (!this.connected) {
        throw new Error('Gateway pub/sub peer is not connected yet.');
      }
    }
  }

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
          return {
            ...builder.build(),
            monitoring: {
              spot: [{ sourceName: SpotServiceNames.spotChannel, intervalMs: 50 }]
            }
          };
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      GatewayPubSubReadiness
    ]
  })(GatewayModule);

  const app = await NestFactory.createApplicationContext(GatewayModule, { logger: false, abortOnError: false });
  const publisher = app.get(ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) as ZLinkSpotPublisherClient;
  const readiness = app.get(GatewayPubSubReadiness);
  const server = await startHttpServer(
    options.httpUrl,
    createGatewayEndpoints(options, evidence, publisher, readiness, () => { stopping = true; })
  );
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
  readiness: { requireConnected(): void },
  stop: () => void
): HttpRoute[] {
  return [
    {
      method: 'GET',
      path: '/health',
      handle: () => {
        readiness.requireConnected();
        return { status: 'ready', role: 'gateway', rid: options.rid };
      }
    },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/spot/publish',
      handle: async (body) => {
        const request = body as SpotPublishReq;
        await publisher
          .publish(SpotServiceNames.spotChannel, SpotServiceNames.spotEventTopic,
            spotServicePacket(SpotMsg, { marker: request.marker }))
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
