import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkLocationRuntimeQuery, type ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLINK_LOCATION_RUNTIME_QUERY, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ChannelNames } from '../../Shared/messages';
import { createRedisLocationStore, storeFailureLocationOptions } from '../../Shared/location-store';
import { validateConsumerOptions } from './Configuration/consumer-options';
import type { ConsumerOptions } from './Configuration/consumer-options';
import { DISCOVERY_OPTIONS, createDiscoveryConfigurationModule } from '../../configuration';
import { createConsumerEndpoints } from './Endpoints/consumer-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startConsumerHost(): Promise<void> {
  let stopping = false;

  const ConsumerModule = createConsumerModule();
  const app = await NestFactory.createApplicationContext(ConsumerModule, { logger: false, abortOnError: false });
  const options = app.get(DISCOVERY_OPTIONS, { strict: false }) as ConsumerOptions;
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const locationQuery = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  const server = await startHttpServer(options.httpUrl, createConsumerEndpoints(channel, locationQuery, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createConsumerModule(): Function {
  class ConsumerModule {}
  const configuration = createDiscoveryConfigurationModule(validateConsumerOptions);
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration], inject: [DISCOVERY_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as ConsumerOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.traceLabel}-flow.log`)
              .traceLabel(options.traceLabel);
          builder.addLocationStore(createRedisLocationStore(options));
          Object.assign(builder.configureLocations(), storeFailureLocationOptions());
          builder.addRouteMesh(ChannelNames.profile).peerConnections();
          return builder.build();
        }
      })
    ]
  })(ConsumerModule);
  return ConsumerModule;
}
