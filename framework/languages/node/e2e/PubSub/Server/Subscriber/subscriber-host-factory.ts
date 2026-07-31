import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_LOCATION_RUNTIME_QUERY, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import type { ZLinkLocationRuntimeQuery } from '@zlink-systems/framework';
import { PacketNames, PubSubNames } from '../../Shared/messages';
import { validateSubscriberOptions, SUBSCRIBER_OPTIONS, type SubscriberOptions } from './Configuration/subscriber-options';
import { PUBSUB_OPTIONS, createPubSubConfigurationModule } from '../../configuration';
import { createSubscriberEndpoints } from './Endpoints/operational-endpoints';
import { EvidenceDispatchErrorObserver, EventMsgHandler, SubscriberSocketEventRecorder } from './Handlers/event-msg-handler';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startSubscriberHost(): Promise<void> {
  let stopping = false;

  const SubscriberModule = createSubscriberModule();
  const app = await NestFactory.createApplicationContext(SubscriberModule, { logger: false, abortOnError: false });
  const options = app.get(PUBSUB_OPTIONS, { strict: false }) as SubscriberOptions;
  const evidence = app.get(EvidenceStore, { strict: false });
  const locations = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  const server = await startHttpServer(
    options.httpUrl,
    createSubscriberEndpoints(evidence, locations, () => { stopping = true; })
  );
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createSubscriberModule(): Function {
  class SubscriberModule {}
  const configuration = createPubSubConfigurationModule(validateSubscriberOptions);
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [PUBSUB_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as SubscriberOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .setMessageFlowObserver(EvidenceDispatchErrorObserver)
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addFanoutChannel(PubSubNames.channel)
            .connect(options.publisherEndpoint)
            .routingId(options.rid)
            .addPublishHandler(PacketNames.eventMsg, EventMsgHandler);
          return {
            ...builder.build(),
            monitoring: { socket: [{ sourceName: `${PubSubNames.channel}.subscriber` }] }
          };
        }
      })
    ],
    providers: [
      {
        provide: EvidenceStore,
        inject: [PUBSUB_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as SubscriberOptions;
          return new EvidenceStore(options.rid, options.evidenceFile);
        }
      },
      { provide: SUBSCRIBER_OPTIONS, useExisting: PUBSUB_OPTIONS },
      EventMsgHandler,
      SubscriberSocketEventRecorder,
      EvidenceDispatchErrorObserver
    ]
  })(SubscriberModule);
  return SubscriberModule;
}
