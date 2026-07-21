import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkActorClient, type ZLinkActorManager, type ZLinkLocationRuntimeQuery, type ZLinkSpotManager, type ZLinkSpotOutbound, type ZLinkSpotHandleResolver } from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER, ZLINK_LOCATION_RUNTIME_QUERY, ZLINK_SPOT_MANAGER, ZLINK_SPOT_OUTBOUND, ZLINK_SPOT_HANDLE_RESOLVER, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SpotServiceNames } from '../../Shared/messages';
import { createSpotServiceConfigurationModule } from '../../configuration';
import { validateMultiNodeOptions } from './Configuration/multi-node-options';
import type { MultiNodeOptions } from './Configuration/multi-node-options';
import { createMultiNodeEndpoints } from './Endpoints/multi-node-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import {
  MultiNodeCreateSpotAHandler,
  MultiNodeCreateSpotBHandler,
  MultiNodeEntrySpot,
  MultiNodeScenarioActorFactory,
  ScaleOutActorProbeHandler,
  MultiNodeSpotOnlyJoinHandler,
  MultiNodeSpotA,
  MultiNodeSpotB,
  MultiNodeStateAHandler,
  MultiNodeStateBHandler,
  SpotOnlyStateMsgHandler,
  SpotOnlyStateReqHandler,
  SpotOnlyUserSpot
} from './Spots/multi-node-spots';
import { closeHttpServer, startHttpServer } from './Support/http-server';

const MULTI_NODE_OPTIONS = Symbol.for('SPOT_SERVICE_MULTI_NODE_OPTIONS');

export async function startMultiNodeHost(): Promise<void> {
  const configuration = createSpotServiceConfigurationModule(MULTI_NODE_OPTIONS, validateMultiNodeOptions);
  const createEvidence = (options: MultiNodeOptions): EvidenceStore => {
    fs.mkdirSync(options.logDir, { recursive: true });
    const evidence = new EvidenceStore(options.rid, options.evidenceFile);
    MultiNodeSpotA.useEvidence(evidence);
    MultiNodeSpotB.useEvidence(evidence);
    MultiNodeEntrySpot.useEvidence(evidence);
    return evidence;
  };
  let stopping = false;

  class MultiNodeModule {}
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [MULTI_NODE_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as MultiNodeOptions;
          const isNodeA = options.rid === SpotServiceNames.multiSpotNodeA;
          const routeChannel = isNodeA ? SpotServiceNames.multiRouteChannelA : SpotServiceNames.multiRouteChannelB;
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          if (options.redisEndpoint !== undefined && options.redisKeyPrefix !== undefined) {
            builder.addLocationStore(new ZLinkRedisLocationStore({
              url: `redis://${options.redisEndpoint}`,
              keyPrefix: options.redisKeyPrefix
            }));
            Object.assign(builder.configureLocations(), {
              pollingIntervalMs: 100,
              heartbeatIntervalMs: 1000,
              ownerLeaseTtlMs: 5000
            });
          } else {
            throw new Error('MultiNode SpotService requires the Redis location store configuration.');
          }
          const route = options.spotOnly
            ? undefined
            : builder.addRouteMesh(routeChannel)
              .listen(options.routeEndpoint)
              .routingId(options.rid);
          route?.channelName(routeChannel);
          route?.peerConnections().connect(options.routeEndpoint);
          const spot = builder.addRouteMesh(options.spotOnly ? SpotServiceNames.spotOnlyMesh : options.rid)
            .routingId(options.rid)
            .listen(options.spotRouterEndpoint)
            .addEntrySpot(MultiNodeEntrySpot)
            .actorFactory(SpotServiceNames.actorType, MultiNodeScenarioActorFactory)
            .addSpotFactory(SpotOnlyUserSpot);
          spot.channelName(options.spotOnly ? SpotServiceNames.spotOnlyMesh : options.rid);
          if (options.peerSpotRouterEndpoint !== undefined) {
            spot.peerConnections().connect(
              isNodeA ? SpotServiceNames.multiSpotNodeB : SpotServiceNames.multiSpotNodeA,
              options.peerSpotRouterEndpoint
            );
          }
          if (isNodeA) {
            route?.addRequestHandler('MultiNodeCreateSpotReq', MultiNodeCreateSpotAHandler);
            spot.addSpotFactory(MultiNodeSpotA);
          } else {
            route?.addRequestHandler('MultiNodeCreateSpotReq', MultiNodeCreateSpotBHandler);
            spot.addSpotFactory(MultiNodeSpotB);
          }
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, inject: [MULTI_NODE_OPTIONS], useFactory: createEvidence },
      MultiNodeCreateSpotAHandler,
      MultiNodeCreateSpotBHandler,
      MultiNodeEntrySpot,
      MultiNodeScenarioActorFactory,
      ScaleOutActorProbeHandler,
      MultiNodeSpotOnlyJoinHandler,
      MultiNodeSpotA,
      MultiNodeSpotB,
      MultiNodeStateAHandler,
      MultiNodeStateBHandler,
      SpotOnlyStateMsgHandler,
      SpotOnlyStateReqHandler,
      SpotOnlyUserSpot
    ]
  })(MultiNodeModule);

  const app = await NestFactory.createApplicationContext(MultiNodeModule, { logger: false, abortOnError: false });
  const options = app.get(MULTI_NODE_OPTIONS) as MultiNodeOptions;
  const evidence = app.get(EvidenceStore);
  const spots = app.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  const outbound = app.get(ZLINK_SPOT_OUTBOUND, { strict: false }) as ZLinkSpotOutbound;
  const spotRefs = app.get(ZLINK_SPOT_HANDLE_RESOLVER, { strict: false }) as ZLinkSpotHandleResolver;
  const actors = app.get(ZLINK_ACTOR_MANAGER, { strict: false }) as ZLinkActorManager;
  const actorClient = app.get(ZLINK_ACTOR_CLIENT, { strict: false }) as ZLinkActorClient;
  const locations = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  SpotOnlyUserSpot.configureDependencies(evidence, spotRefs);
  const server = await startHttpServer(options.httpUrl, createMultiNodeEndpoints(
    evidence,
    spots,
    outbound,
    spotRefs,
    actors,
    actorClient,
    locations,
    options.spotOnly ? SpotServiceNames.spotOnlyMesh : options.rid,
    () => { stopping = true; }
  ));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
