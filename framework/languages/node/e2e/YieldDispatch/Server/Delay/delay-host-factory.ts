import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { YieldDispatchNames } from '../../Shared/messages';
import { EvidenceStore } from './Support/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { parseDelayOptions } from './Configuration/delay-options';
import { DelayHandler } from './Handlers/delay-handler';

export async function startDelayHost(args: readonly string[]): Promise<void> {
  const options = parseDelayOptions(args);
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  let stopping = false;

  class DelayModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .addClientServerChannel(YieldDispatchNames.delayChannel)
            .enableServer(options.delayEndpoint)
            .routingId(options.rid)
            .addRequestHandler('DelayReq', DelayHandler)
          .build()
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      DelayHandler
    ]
  })(DelayModule);

  const app = await NestFactory.createApplicationContext(DelayModule, { logger: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'delay', rid: options.rid }) },
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
