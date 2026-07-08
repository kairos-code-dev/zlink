import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import {
  ZLINK_CHANNEL_CLIENT,
  ZLINK_ROUTE_CLIENT,
  ZLinkModule,
  zlinkFramework
} from '@zlink-systems/nestjs';
import type { ZLinkChannelClient, ZLinkRouteClient } from '@zlink-systems/framework';
import { createRedisLocationStore, locationMessagingOptions } from '../../Shared/location-store';
import { PacketNames } from '../../Shared/messages';
import { parseServerOptions } from './Configuration/server-options';
import type { ServerOptions } from './Configuration/server-options';
import { createProviderEndpoints } from './Endpoints/provider-endpoints';
import {
  EvidenceDispatchErrorObserver,
  PayloadRequestHandler,
  ProfileCommandHandler,
  ProfileRequestHandler,
  RoutePingHandler
} from './Handlers/provider-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startProviderHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  const ProviderModule = createProviderModule(options, evidence);
  const app = await NestFactory.createApplicationContext(ProviderModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const route = app.get(ZLINK_ROUTE_CLIENT, { strict: false }) as ZLinkRouteClient;
  const server = await startHttpServer(options.httpUrl, createProviderEndpoints(evidence, channel, route, () => { stopping = true; }));

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createProviderModule(options: ServerOptions, evidence: EvidenceStore): Function {
  class ProviderModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .setMessageFlowObserver(EvidenceDispatchErrorObserver)
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          if (options.redisEndpoint !== undefined && options.redisKeyPrefix !== undefined) {
            builder.addLocationStore(createRedisLocationStore({
              redisEndpoint: options.redisEndpoint,
              redisKeyPrefix: options.redisKeyPrefix
            }));
            Object.assign(builder.configureLocations(), locationMessagingOptions());
          }
          if (options.channelEndpoint !== undefined) {
            const profile = builder.addClientServerChannel('profile')
              .enableServer(options.channelEndpoint)
              .routingId(options.rid)
              .enableClient();
            profile.configureServerSocket().weight = options.weight;
            if (options.maxMessageSize > 0) {
              profile.configureServerSocket().maxMessageSize = options.maxMessageSize;
            }
            profile
              .addRequestHandler(PacketNames.profileReq, ProfileRequestHandler)
              .addRequestHandler(PacketNames.payloadReq, PayloadRequestHandler)
              .addSendHandler(PacketNames.profileMsg, ProfileCommandHandler);
          }
          if (options.manualClientEndpoint !== undefined) {
            builder.addClientServerChannel('profile.manual')
              .enableClient(options.manualClientEndpoint);
          }
          if (options.routeEndpoint !== undefined) {
            builder.addRouteMeshChannel('profile.route')
              .enableRouter(options.routeEndpoint)
              .routingId(options.rid)
              .connect(options.routePeers)
              .addRequestHandler(PacketNames.scenarioRouteReq, RoutePingHandler);
          }
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      EvidenceDispatchErrorObserver,
      PayloadRequestHandler,
      ProfileCommandHandler,
      ProfileRequestHandler,
      RoutePingHandler
    ]
  })(ProviderModule);
  return ProviderModule;
}
