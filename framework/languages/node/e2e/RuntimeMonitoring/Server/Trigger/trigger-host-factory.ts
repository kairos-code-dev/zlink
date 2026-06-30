import fs from 'node:fs';
import path from 'node:path';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import {
  ZLINK_CHANNEL_CLIENT,
  ZLinkModule,
  zlinkFramework
} from '@zlink-systems/nestjs';
import type { ProfileReply, ProfileRequest } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import { parseTriggerOptions } from './Configuration/trigger-options';
import type { TriggerOptions } from './Configuration/trigger-options';
import { createTriggerEndpoints, requestProfile } from './Endpoints/trigger-endpoints';
import { TriggerSocketEventRecorder } from './Handlers/trigger-event-recorders';
import { EvidenceStore } from '../Service/Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startTriggerHost(args: readonly string[]): Promise<void> {
  const options = parseTriggerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(path.join(options.logDir, 'trigger.evidence.log'));
  let stopping = false;

  const TriggerModule = createTriggerModule(options, evidence);
  const app = await NestFactory.createApplicationContext(TriggerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(
    options.httpUrl,
    createTriggerEndpoints(options, channel, evidence, (request, endpoint) => requestWithTransientHost(options, request, endpoint), () => { stopping = true; })
  );

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

async function requestWithTransientHost(
  options: TriggerOptions,
  request: ProfileRequest,
  channelEndpoint = options.serviceChannelEndpoint
): Promise<ProfileReply> {
  const traceLabel = `trigger-${request.marker}`;
  const evidence = new EvidenceStore(path.join(options.logDir, `${traceLabel}.evidence.log`));
  const TriggerModule = createTriggerModule(options, evidence, traceLabel, channelEndpoint);
  const app = await NestFactory.createApplicationContext(TriggerModule, { logger: false, abortOnError: false });
  try {
    const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    return await requestProfile(channel, request);
  } finally {
    await Promise.race([
      app.close(),
      new Promise<void>((resolve) => setTimeout(resolve, 5000))
    ]);
  }
}

function createTriggerModule(
  options: TriggerOptions,
  evidence: EvidenceStore,
  traceLabel = 'trigger',
  channelEndpoint = options.serviceChannelEndpoint
): Function {
  class TriggerModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${traceLabel}-flow.log`)
              .traceLabel(traceLabel);

          builder.addClientServerChannel(RuntimeMonitoringNames.channel)
            .enableClient([channelEndpoint]);
          return {
            ...builder.build(),
            monitoring: {
              socket: [{ sourceName: RuntimeMonitoringNames.channelClientSource }]
            }
          };
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      TriggerSocketEventRecorder
    ]
  })(TriggerModule);
  return TriggerModule;
}
