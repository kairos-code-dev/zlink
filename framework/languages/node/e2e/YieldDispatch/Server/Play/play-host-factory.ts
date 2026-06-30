import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { YieldDispatchNames } from '../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import { parsePlayOptions } from './Configuration/play-options';
import { HoldCommandHandler, ProbeCommandHandler, WorkerYieldCommandHandler, YieldCommandHandler, YieldRequestHandler } from './Handlers/basic-spot-handlers';
import {
  BindYieldActorsControlHandler,
  EnsureSpotControlHandler,
  YieldEvidenceControlHandler,
  YieldEvidenceWaitControlHandler
} from './Handlers/control-handlers';
import { YieldCancelCommandHandler, YieldTimeoutCommandHandler } from './Handlers/failure-spot-handlers';
import { RemoteSpotYieldCommandHandler, RemoteSpotYieldHandler } from './Handlers/remote-spot-handlers';
import { TimerStartCommandHandler, TimerStopCommandHandler, YieldTimerHandler } from './Handlers/timer-spot-handlers';
import {
  EntryActorFastHandler,
  EntryActorFastSendHandler,
  EntryActorJoinYieldHandler,
  EntryActorPushYieldHandler,
  EntryActorYieldHandler,
  SpotActorFastHandler,
  SpotActorFastSendHandler,
  SpotActorPushYieldHandler,
  SpotActorYieldHandler,
  YieldActorFactory,
  YieldEntrySpot
} from './Spots/yield-actors';
import { YieldProbeSpot } from './Spots/yield-probe-spot';

export async function startPlayHost(args: readonly string[]): Promise<void> {
  const options = parsePlayOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  let stopping = false;

  class PlayModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .options({
              registrySpotRemoteAddresses: {
                namespace: YieldDispatchNames.spotChannel,
                routerChannelId: YieldDispatchNames.spotRouteChannel
              }
            })
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
          builder.addRouteMesh(YieldDispatchNames.controlChannel)
            .enableRouter(options.controlEndpoint)
            .routingId(options.rid)
            .addRequestHandler('EnsureSpotReq', EnsureSpotControlHandler)
            .addRequestHandler('BindYieldActorsReq', BindYieldActorsControlHandler)
            .addRequestHandler('YieldEvidenceReq', YieldEvidenceControlHandler)
            .addRequestHandler('YieldEvidenceWaitReq', YieldEvidenceWaitControlHandler);
          builder.addClientServerChannel(YieldDispatchNames.delayChannel)
            .enableClient(options.delayEndpoint);
          builder.addRouteMesh(YieldDispatchNames.spotRouteChannel)
            .enableRouter(options.spotRouteEndpoint)
            .routingId(options.rid);
          builder.addSpotMesh(YieldDispatchNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .enablePubSub(options.spotPubEndpoint)
            .addEntrySpot(YieldEntrySpot)
            .actorFactory(YieldDispatchNames.actorType, YieldActorFactory)
            .addSpotFactory(YieldProbeSpot);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      EnsureSpotControlHandler,
      BindYieldActorsControlHandler,
      YieldEvidenceControlHandler,
      YieldEvidenceWaitControlHandler,
      HoldCommandHandler,
      YieldCommandHandler,
      YieldRequestHandler,
      WorkerYieldCommandHandler,
      YieldTimeoutCommandHandler,
      YieldCancelCommandHandler,
      ProbeCommandHandler,
      RemoteSpotYieldHandler,
      RemoteSpotYieldCommandHandler,
      TimerStartCommandHandler,
      TimerStopCommandHandler,
      YieldTimerHandler,
      YieldActorFactory,
      YieldEntrySpot,
      EntryActorYieldHandler,
      EntryActorFastHandler,
      EntryActorFastSendHandler,
      EntryActorJoinYieldHandler,
      EntryActorPushYieldHandler,
      SpotActorYieldHandler,
      SpotActorFastHandler,
      SpotActorFastSendHandler,
      SpotActorPushYieldHandler,
      YieldProbeSpot
    ]
  })(PlayModule);

  const app = await NestFactory.createApplicationContext(PlayModule, { logger: false, abortOnError: false });
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
