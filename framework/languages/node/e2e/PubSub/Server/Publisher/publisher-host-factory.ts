import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkFanoutClient } from '@zlink-systems/framework';
import { ZLINK_FANOUT_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PubSubNames } from '../../Shared/messages';
import { parsePublisherOptions } from './Configuration/publisher-options';
import { createPublisherEndpoints } from './Endpoints/publisher-endpoints';
import { EvidenceDispatchErrorObserver } from './Handlers/evidence-dispatch-error-observer';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startPublisherHost(args: readonly string[]): Promise<void> {
  const options = parsePublisherOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  class PublisherModule {}
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
          builder.useInMemoryLocationStores();
          builder.addFanoutChannel(PubSubNames.channel)
            .enablePublisher(options.publisherEndpoint);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      EvidenceDispatchErrorObserver
    ]
  })(PublisherModule);

  const app = await NestFactory.createApplicationContext(PublisherModule, { logger: false, abortOnError: false });
  const fanout = app.get(ZLINK_FANOUT_CLIENT, { strict: false }) as ZLinkFanoutClient;
  const server = await startHttpServer(options.httpUrl, createPublisherEndpoints(fanout, evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
