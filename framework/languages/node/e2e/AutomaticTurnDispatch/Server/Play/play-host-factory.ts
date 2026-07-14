import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { EvidenceStore } from './Support/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { createAutomaticTurnConfigurationModule } from '../../configuration';
import { PLAY_OPTIONS, validatePlayOptions } from './Configuration/play-options';
import type { PlayOptions } from './Configuration/play-options';
import { HoldCommandHandler, ProbeCommandHandler, WorkerAwaitCommandHandler, AwaitCommandHandler, AwaitRequestHandler } from './Handlers/basic-spot-handlers';
import {
  BindAwaitActorsControlHandler,
  EnsureSpotControlHandler,
  YIELD_PLAY_NODE_RID,
  AwaitEvidenceControlHandler,
  AwaitEvidenceWaitControlHandler
} from './Handlers/control-handlers';
import { AwaitCancelCommandHandler, AwaitTimeoutCommandHandler } from './Handlers/failure-spot-handlers';
import { RemoteSpotAwaitCommandHandler, RemoteSpotAwaitHandler } from './Handlers/remote-spot-handlers';
import { TimerStartCommandHandler, TimerStopCommandHandler, AwaitTimerHandler } from './Handlers/timer-spot-handlers';
import {
  EntryActorFastHandler,
  EntryActorFastSendHandler,
  EntryActorJoinAwaitHandler,
  EntryActorPushAwaitHandler,
  EntryActorAwaitHandler,
  SpotActorFastHandler,
  SpotActorFastSendHandler,
  SpotActorPushAwaitHandler,
  SpotActorAwaitHandler,
  AwaitActorFactory,
  AwaitEntrySpot
} from './Spots/await-actors';
import { AwaitProbeSpot } from './Spots/await-probe-spot';

export async function startPlayHost(): Promise<void> {
  let stopping = false;
  const configuration = createAutomaticTurnConfigurationModule(PLAY_OPTIONS, validatePlayOptions);

  class PlayModule {}
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [PLAY_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as PlayOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const locationStore = new ZLinkRedisLocationStore({
            url: `redis://${options.redisEndpoint}`,
            keyPrefix: options.redisKeyPrefix
          });
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addLocationStore(locationStore);
          builder.addRouteMeshChannel(AutomaticTurnDispatchNames.controlChannel)
            .enableRouter(options.controlEndpoint)
            .routingId(options.rid)
            .addRequestHandler('EnsureSpotReq', EnsureSpotControlHandler)
            .addRequestHandler('BindAwaitActorsReq', BindAwaitActorsControlHandler)
            .addRequestHandler('AwaitEvidenceReq', AwaitEvidenceControlHandler)
            .addRequestHandler('AwaitEvidenceWaitReq', AwaitEvidenceWaitControlHandler);
          builder.addClientServerChannel(AutomaticTurnDispatchNames.delayChannel)
            .enableClient(options.delayEndpoint);
          const spotRoute = builder.addRouteMeshChannel(AutomaticTurnDispatchNames.spotRouteChannel)
            .enableRouter(options.spotRouteEndpoint)
            .routingId(options.rid);
          if (options.peerSpotRouteEndpoints.length > 0) {
            spotRoute.connect(options.peerSpotRouteEndpoints);
          }
          const spotMesh = builder.addSpotMesh(AutomaticTurnDispatchNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .enablePubSub(options.spotPubEndpoint)
            .addEntrySpot(AwaitEntrySpot)
            .actorFactory(AutomaticTurnDispatchNames.actorType, AwaitActorFactory)
            .addSpotFactory(AwaitProbeSpot);
          for (const peer of options.spotRouterPeers) spotMesh.connectRouter(peer.rid, peer.endpoint);
          return builder.build();
        }
      })
    ],
    providers: [
      {
        provide: EvidenceStore,
        inject: [PLAY_OPTIONS],
        useFactory: (options: PlayOptions) => new EvidenceStore(options.rid, options.evidenceFile)
      },
      {
        provide: YIELD_PLAY_NODE_RID,
        inject: [PLAY_OPTIONS],
        useFactory: (options: PlayOptions) => options.rid
      },
      EnsureSpotControlHandler,
      BindAwaitActorsControlHandler,
      AwaitEvidenceControlHandler,
      AwaitEvidenceWaitControlHandler,
      HoldCommandHandler,
      AwaitCommandHandler,
      AwaitRequestHandler,
      WorkerAwaitCommandHandler,
      AwaitTimeoutCommandHandler,
      AwaitCancelCommandHandler,
      ProbeCommandHandler,
      RemoteSpotAwaitHandler,
      RemoteSpotAwaitCommandHandler,
      TimerStartCommandHandler,
      TimerStopCommandHandler,
      AwaitTimerHandler,
      AwaitActorFactory,
      AwaitEntrySpot,
      EntryActorAwaitHandler,
      EntryActorFastHandler,
      EntryActorFastSendHandler,
      EntryActorJoinAwaitHandler,
      EntryActorPushAwaitHandler,
      SpotActorAwaitHandler,
      SpotActorFastHandler,
      SpotActorFastSendHandler,
      SpotActorPushAwaitHandler,
      AwaitProbeSpot
    ]
  })(PlayModule);

  const app = await NestFactory.createApplicationContext(PlayModule, { logger: false, abortOnError: false });
  const options = app.get<PlayOptions>(PLAY_OPTIONS);
  const evidence = app.get(EvidenceStore);
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'play', rid: options.rid }) },
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
}
