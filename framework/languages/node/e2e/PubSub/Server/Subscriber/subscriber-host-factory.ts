import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames, PubSubNames } from '../../Shared/messages';
import { parseSubscriberOptions, SUBSCRIBER_OPTIONS, type SubscriberOptions } from './Configuration/subscriber-options';
import { createSubscriberEndpoints } from './Endpoints/operational-endpoints';
import { EvidenceDispatchErrorObserver, EventMsgHandler } from './Handlers/event-msg-handler';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startSubscriberHost(args: readonly string[]): Promise<void> {
  const options = parseSubscriberOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  const SubscriberModule = createSubscriberModule(options, evidence);
  const app = await NestFactory.createApplicationContext(SubscriberModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, createSubscriberEndpoints(evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createSubscriberModule(options: SubscriberOptions, evidence: EvidenceStore): Function {
  class SubscriberModule {}
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
            .enableSubscriber(options.publisherEndpoint)
            .addPublishHandler(PacketNames.eventMsg, EventMsgHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      { provide: SUBSCRIBER_OPTIONS, useValue: options },
      EventMsgHandler,
      EvidenceDispatchErrorObserver
    ]
  })(SubscriberModule);
  return SubscriberModule;
}
