import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkSpotManager, type ZLinkSpotOutbound } from '@zlink-systems/framework';
import { ZLINK_SPOT_MANAGER, ZLINK_SPOT_OUTBOUND, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SpotServiceNames } from '../../Shared/messages';
import { parseMultiNodeOptions } from './Configuration/multi-node-options';
import { createMultiNodeEndpoints } from './Endpoints/multi-node-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import {
  MultiNodeCreateSpotAHandler,
  MultiNodeCreateSpotBHandler,
  MultiNodeSpotA,
  MultiNodeSpotB,
  MultiNodeStateAHandler,
  MultiNodeStateBHandler
} from './Spots/multi-node-spots';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startMultiNodeHost(args: readonly string[]): Promise<void> {
  const options = parseMultiNodeOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  MultiNodeSpotA.useEvidence(evidence);
  MultiNodeSpotB.useEvidence(evidence);
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
            .options({
              registrySpotRemoteAddresses: {
                namespace: options.rid,
                routerChannelId: routeChannel
              }
            })
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
          const route = builder.addRouteMesh(routeChannel)
            .enableRouter(options.routeEndpoint)
            .routingId(options.rid)
            .connect(options.routeEndpoint);
          const spot = builder.addSpotMesh(options.rid)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint);
          if (isNodeA) {
            route.addRequestHandler('MultiNodeCreateSpotReq', MultiNodeCreateSpotAHandler);
            spot.addSpotFactory(MultiNodeSpotA);
          } else {
            route.addRequestHandler('MultiNodeCreateSpotReq', MultiNodeCreateSpotBHandler);
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
      MultiNodeSpotA,
      MultiNodeSpotB,
      MultiNodeStateAHandler,
      MultiNodeStateBHandler
    ]
  })(MultiNodeModule);

  const app = await NestFactory.createApplicationContext(MultiNodeModule, { logger: false, abortOnError: false });
  const spots = app.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  const outbound = app.get(ZLINK_SPOT_OUTBOUND, { strict: false }) as ZLinkSpotOutbound;
  const server = await startHttpServer(options.httpUrl, createMultiNodeEndpoints(evidence, spots, outbound, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
