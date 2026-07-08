import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkActorClient, type ZLinkActorManager, type ZLinkSpotManager, type ZLinkSpotOutbound, type ZLinkSpotRefResolver } from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER, ZLINK_SPOT_MANAGER, ZLINK_SPOT_OUTBOUND, ZLINK_SPOT_REF_RESOLVER, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SpotServiceNames } from '../../Shared/messages';
import { parseMultiNodeOptions } from './Configuration/multi-node-options';
import { createMultiNodeEndpoints } from './Endpoints/multi-node-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import {
  MultiNodeCreateSpotAHandler,
  MultiNodeCreateSpotBHandler,
  MultiNodeEntrySpot,
  MultiNodeScenarioActorFactory,
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

export async function startMultiNodeHost(args: readonly string[]): Promise<void> {
  const options = parseMultiNodeOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  MultiNodeSpotA.useEvidence(evidence);
  MultiNodeSpotB.useEvidence(evidence);
  MultiNodeEntrySpot.useEvidence(evidence);
  let stopping = false;

  const isNodeA = options.rid === SpotServiceNames.multiSpotNodeA;
  const routeChannel = isNodeA ? SpotServiceNames.multiRouteChannelA : SpotServiceNames.multiRouteChannelB;

  class MultiNodeModule {}
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
            builder.useInMemoryLocationStores();
          }
          const route = options.spotOnly
            ? undefined
            : builder.addRouteMeshChannel(routeChannel)
              .enableRouter(options.routeEndpoint)
              .routingId(options.rid)
              .connect(options.routeEndpoint);
          const spot = builder.addSpotMesh(options.spotOnly ? SpotServiceNames.spotOnlyMesh : options.rid)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .addEntrySpot(MultiNodeEntrySpot)
            .actorFactory(SpotServiceNames.actorType, MultiNodeScenarioActorFactory)
            .addSpotFactory(SpotOnlyUserSpot);
          if (options.spotPubEndpoint !== undefined) {
            spot.enablePubSub(options.spotPubEndpoint);
          }
          if (options.peerSpotRouterEndpoint !== undefined) {
            spot.connectRouter(
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
      { provide: EvidenceStore, useValue: evidence },
      MultiNodeCreateSpotAHandler,
      MultiNodeCreateSpotBHandler,
      MultiNodeEntrySpot,
      MultiNodeScenarioActorFactory,
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
  const spots = app.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  const outbound = app.get(ZLINK_SPOT_OUTBOUND, { strict: false }) as ZLinkSpotOutbound;
  const spotRefs = app.get(ZLINK_SPOT_REF_RESOLVER, { strict: false }) as ZLinkSpotRefResolver;
  const actors = app.get(ZLINK_ACTOR_MANAGER, { strict: false }) as ZLinkActorManager;
  const actorClient = app.get(ZLINK_ACTOR_CLIENT, { strict: false }) as ZLinkActorClient;
  SpotOnlyUserSpot.configureDependencies(evidence, spotRefs);
  const server = await startHttpServer(options.httpUrl, createMultiNodeEndpoints(evidence, spots, outbound, spotRefs, actors, actorClient, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
