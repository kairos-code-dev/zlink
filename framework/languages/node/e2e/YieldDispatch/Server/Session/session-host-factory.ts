import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { YieldDispatchNames } from '../../Shared/messages';
import { EvidenceStore } from './Support/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { parseSessionOptions } from './Configuration/session-options';
import { YieldSessionFactory } from './Handlers/yield-session';

export async function startSessionHost(args: readonly string[]): Promise<void> {
  const options = parseSessionOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  let stopping = false;

  class SessionModule {}
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
            .useDiscovery()
              .addRegistryEndpoint(options.registryRouterEndpoint)
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          builder.addRouteMesh(YieldDispatchNames.controlChannel)
            .enableRouter(options.controlRouterEndpoint)
            .routingId(options.rid)
            .connect(options.playControlEndpoints);
          builder.addRouteMesh(YieldDispatchNames.spotRouteChannel)
            .enableRouter(options.spotRouteEndpoint)
            .routingId(options.rid)
            .connect(options.playSpotRouteEndpoints);
          builder.addSpotMesh(YieldDispatchNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint);
          builder.addStreamNode(YieldDispatchNames.streamNode)
            .bind(options.streamEndpoint)
            .registerSession(YieldSessionFactory);

          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      YieldSessionFactory
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
}
