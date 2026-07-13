import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessageFlowLogMode,
  type ZLinkActor,
  type ZLinkEntrySpot,
  type ZLinkEntrySpotContext,
  type ZLinkMessage,
  type ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { EvidenceStore } from './Support/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { parseSessionOptions } from './Configuration/session-options';
import { AwaitSessionFactory } from './Handlers/await-session';

class AwaitSessionEntrySpot implements ZLinkEntrySpot {
  readonly context!: ZLinkEntrySpotContext;

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    void actorId;
    void request;
    return { accepted: true };
  }

  async onJoinedActor(actor: ZLinkActor): Promise<void> { void actor; }

  async onLeaveActor(actor: ZLinkActor): Promise<void> { void actor; }
}

export async function startSessionHost(args: readonly string[]): Promise<void> {
  const options = parseSessionOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  const locationStore = new ZLinkRedisLocationStore({
    url: `redis://${options.redisEndpoint}`,
    keyPrefix: options.redisKeyPrefix
  });
  let stopping = false;

  class SessionModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .addLocationStore(locationStore)
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          builder.addRouteMeshChannel(AutomaticTurnDispatchNames.controlChannel)
            .enableRouter(options.controlRouterEndpoint)
            .routingId(options.rid)
            .connect(options.playControlEndpoints);
          builder.addRouteMeshChannel(AutomaticTurnDispatchNames.spotRouteChannel)
            .enableRouter(options.spotRouteEndpoint)
            .routingId(options.rid)
            .connect(options.playSpotRouteEndpoints);
          const spotMesh = builder.addSpotMesh(AutomaticTurnDispatchNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .addEntrySpot(AwaitSessionEntrySpot);
          for (const peer of options.spotRouterPeers) spotMesh.connectRouter(peer.rid, peer.endpoint);
          builder.addStreamNode(AutomaticTurnDispatchNames.streamNode)
            .bind(options.streamEndpoint)
            .registerSession(AwaitSessionFactory);

          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      AwaitSessionEntrySpot,
      AwaitSessionFactory
    ]
  })(SessionModule);

  const app = await NestFactory.createApplicationContext(SessionModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'session', rid: options.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/shutdown',
      handle: () => {
        stopping = true;
        return { status: 'stopping' };
      }
    }
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
  await locationStore.dispose();
}
