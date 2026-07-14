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
import type { ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import { validateTriggerOptions } from './Configuration/trigger-options';
import type { TriggerOptions } from './Configuration/trigger-options';
import { MONITORING_OPTIONS, createMonitoringConfigurationModule } from '../../configuration';
import { createTriggerEndpoints, requestProfile } from './Endpoints/trigger-endpoints';
import { TriggerSocketEventRecorder } from './Handlers/trigger-event-recorders';
import { EvidenceStore } from '../Service/Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startTriggerHost(): Promise<void> {
  let stopping = false;

  const TriggerModule = createConfiguredTriggerModule();
  const app = await NestFactory.createApplicationContext(TriggerModule, { logger: false, abortOnError: false });
  const options = app.get(MONITORING_OPTIONS, { strict: false }) as TriggerOptions;
  const evidence = app.get(EvidenceStore, { strict: false });
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

function createConfiguredTriggerModule(): Function {
  class TriggerModule {}
  const configuration = createMonitoringConfigurationModule(validateTriggerOptions);
  Module({
    imports: [configuration, ZLinkModule.forRootFactory({
      imports: [configuration], inject: [MONITORING_OPTIONS],
      useFactory: (value: unknown) => buildTriggerFramework(value as TriggerOptions)
    })],
    providers: [
      { provide: EvidenceStore, inject: [MONITORING_OPTIONS], useFactory: (value: unknown) => {
        const options = value as TriggerOptions; return new EvidenceStore('trigger', path.join(options.logDir, 'trigger.evidence.log'));
      } },
      TriggerSocketEventRecorder
    ]
  })(TriggerModule);
  return TriggerModule;
}

async function requestWithTransientHost(
  options: TriggerOptions,
  request: ProfileReq,
  channelEndpoint = options.serviceChannelEndpoint
): Promise<ProfileRes> {
  const traceLabel = `trigger-${request.marker}`;
  const evidence = new EvidenceStore(traceLabel, path.join(options.logDir, `${traceLabel}.evidence.log`));
  const TriggerModule = createTriggerModule(options, evidence, traceLabel, channelEndpoint);
  const app = await NestFactory.createApplicationContext(TriggerModule, { logger: false, abortOnError: false });
  try {
    const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    await waitForTransientChannelReady(evidence);
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
        useFactory: () => buildTriggerFramework(options, traceLabel, channelEndpoint)
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      TriggerSocketEventRecorder
    ]
  })(TriggerModule);
  return TriggerModule;
}

function buildTriggerFramework(options: TriggerOptions, traceLabel = 'trigger', channelEndpoint = options.serviceChannelEndpoint) {
  fs.mkdirSync(options.logDir, { recursive: true });
  const builder = zlinkFramework();
  builder.configureDispatch().messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
    .traceLogFile(`${options.logDir}/${traceLabel}-flow.log`).traceLabel(traceLabel);
  builder.addClientServerChannel(RuntimeMonitoringNames.channel).enableClient([channelEndpoint]);
  return { ...builder.build(), monitoring: { socket: [{ sourceName: RuntimeMonitoringNames.channelClientSource }] } };
}

async function waitForTransientChannelReady(evidence: EvidenceStore): Promise<void> {
  const entries = await evidence.waitUntil((snapshot) =>
    snapshot.some((line) =>
      line.includes('monitor-socket|')
      && line.includes('kind=connectionReady')
    ), 10000);
  if (!entries.some((line) => line.includes('kind=connectionReady'))) {
    throw new Error('Timed out waiting for transient trigger channel readiness.');
  }
}
