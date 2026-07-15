import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import type { ZLinkRouteClient, ZLinkSpotManager, ZLinkSpotOutbound, ZLinkSpotHandleResolver } from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_MANAGER, ZLINK_SPOT_OUTBOUND, ZLINK_SPOT_HANDLE_RESOLVER, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SpotServiceNames } from '../../Shared/messages';
import { createSpotServiceConfigurationModule } from '../../configuration';
import { validatePlayOptions } from './Configuration/play-options';
import type { PlayOptions } from './Configuration/play-options';
import { createPlayEndpoints } from './Endpoints/play-endpoints';
import { ChannelEchoHandler, ChannelNotifyHandler } from './Handlers/channel-handlers';
import { ControlPingHandler, CreateSpotHandler, CrossRoleActorPushHandler, EnsureActorHandler } from './Handlers/control-handlers';
import { EvidenceDispatchErrorObserver } from './Handlers/dispatch-error-observer';
import { SpotMsgHandler, SpotOutboundHandler, SpotOutboundNegativeHandler } from './Handlers/spot-outbound-handlers';
import { SpotToSpotHandler, SpotToSpotNegativeHandler, SpotToSpotTimeoutHandler } from './Handlers/spot-to-spot-handlers';
import { StageProbeHandler, StageTimerHandler, StageTimerStartHandler } from './Handlers/stage-handlers';
import { SlowSpotHandler, StateCommandHandler, StateReqHandler } from './Handlers/state-req-handler';
import { SpotAdminHandler } from './Handlers/spot-admin-handler';
import { BasicTimerHandler, IdleCloseTimerHandler, OverrunTimerHandler } from './Handlers/timer-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import {
  ComplexActorHandler,
  EntryActorDestroyHandler,
  EntryActorLeaveHandler,
  EntryActorPingHandler,
  EntrySlowActorPingHandler,
  EntryUserActorPingHandler,
  EntryUserActorPushHandler,
  EntryUserSpotActorJoinHandler,
  ScenarioActorFactory,
  ScenarioEntrySpot
} from './Spots/scenario-actors';
import {
  ScenarioAlternateSpot,
  ScenarioUserSpot,
  UserActorLeaveHandler,
  UserActorPingHandler,
  UserActorPushHandler
} from './Spots/scenario-spots';
import { closeHttpServer, startHttpServer } from './Support/http-server';

const PLAY_OPTIONS = Symbol.for('SPOT_SERVICE_PLAY_OPTIONS');

export async function startPlayHost(): Promise<void> {
  const configuration = createSpotServiceConfigurationModule(PLAY_OPTIONS, validatePlayOptions);
  const createEvidence = (options: PlayOptions): EvidenceStore => {
    fs.mkdirSync(options.logDir, { recursive: true });
    const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  ScenarioEntrySpot.useEvidence(evidence);
  ScenarioUserSpot.useEvidence(evidence);
  EntryActorPingHandler.useEvidence(evidence);
  EntrySlowActorPingHandler.useEvidence(evidence);
  EntryUserActorPingHandler.useEvidence(evidence);
  ComplexActorHandler.useEvidence(evidence);
  EntryActorLeaveHandler.useEvidence(evidence);
  EntryActorDestroyHandler.useEvidence(evidence);
    return evidence;
  };
  let stopping = false;

  class PlayModule {}
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [PLAY_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as PlayOptions;
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .setMessageFlowObserver(EvidenceDispatchErrorObserver)
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          builder.useInMemoryLocationStores();
          builder.addRouteMeshChannel(SpotServiceNames.controlChannel)
            .enableRouter(options.controlRouterEndpoint)
            .routingId(options.rid)
            .addRequestHandler('ControlPingReq', ControlPingHandler)
            .addRequestHandler('EnsureActorReq', EnsureActorHandler)
            .addRequestHandler('CrossRoleActorPushReq', CrossRoleActorPushHandler)
            .addRequestHandler('CreateSpotReq', CreateSpotHandler);
          const externalSpotChannel = options.rid === 'play-b'
            ? SpotServiceNames.externalSpotChannelB
            : SpotServiceNames.externalSpotChannel;
          builder.addRouteMeshChannel(externalSpotChannel)
            .enableRouter(options.externalSpotEndpoint)
            .routingId(options.rid)
            .addRequestHandler('ChannelEchoReq', ChannelEchoHandler)
            .addRequestHandler('CrossRoleActorPushReq', CrossRoleActorPushHandler);
          if (options.rid === 'play-b' && options.playAExternalSpotEndpoint !== undefined) {
            builder.addRouteMeshChannel(SpotServiceNames.externalSpotChannel)
              .connect(options.playAExternalSpotEndpoint);
          }
          const spot = builder.addSpotMesh(SpotServiceNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .enablePubSub(options.spotPubEndpoint)
            .addEntrySpot(ScenarioEntrySpot)
            .addSpotFactory(ScenarioUserSpot)
            .addSpotFactory(ScenarioAlternateSpot)
            .actorFactory(SpotServiceNames.actorType, ScenarioActorFactory);
          for (const endpoint of options.clientSpotPubEndpoints) {
            spot.connectPeerPub(endpoint);
          }
          if (options.externalClientEndpoint !== undefined) {
            builder.addClientServerChannel(SpotServiceNames.externalClientChannel)
              .enableServer(options.externalClientEndpoint)
              .routingId(options.rid)
              .enableClient(options.externalClientEndpoint)
              .addRequestHandler('ChannelEchoReq', ChannelEchoHandler)
              .addSendHandler('ChannelNotify', ChannelNotifyHandler);
          }

          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, inject: [PLAY_OPTIONS], useFactory: createEvidence },
      EvidenceDispatchErrorObserver,
      ChannelEchoHandler,
      ChannelNotifyHandler,
      ControlPingHandler,
      CrossRoleActorPushHandler,
      CreateSpotHandler,
      EnsureActorHandler,
      ScenarioEntrySpot,
      ScenarioActorFactory,
      EntryActorPingHandler,
      EntrySlowActorPingHandler,
      EntryUserActorPingHandler,
      EntryUserActorPushHandler,
      EntryUserSpotActorJoinHandler,
      ComplexActorHandler,
      EntryActorLeaveHandler,
      EntryActorDestroyHandler,
      StateReqHandler,
      SpotAdminHandler,
      StateCommandHandler,
      StageProbeHandler,
      StageTimerStartHandler,
      StageTimerHandler,
      SlowSpotHandler,
      SpotOutboundHandler,
      SpotOutboundNegativeHandler,
      SpotToSpotHandler,
      SpotToSpotTimeoutHandler,
      SpotToSpotNegativeHandler,
      SpotMsgHandler,
      UserActorLeaveHandler,
      UserActorPingHandler,
      UserActorPushHandler,
      BasicTimerHandler,
      IdleCloseTimerHandler,
      OverrunTimerHandler
    ]
  })(PlayModule);

  const app = await NestFactory.createApplicationContext(PlayModule, { logger: false, abortOnError: false });
  const options = app.get(PLAY_OPTIONS) as PlayOptions;
  const evidence = app.get(EvidenceStore);
  const spotManager = app.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  const spotOutbound = app.get(ZLINK_SPOT_OUTBOUND, { strict: false }) as ZLinkSpotOutbound;
  const spotRefs = app.get(ZLINK_SPOT_HANDLE_RESOLVER, { strict: false }) as ZLinkSpotHandleResolver;
  const routeClient = app.get(ZLINK_ROUTE_CLIENT, { strict: false }) as ZLinkRouteClient;
  const server = await startHttpServer(options.httpUrl, createPlayEndpoints(evidence, spotManager, spotOutbound, spotRefs, routeClient, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
