import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkLocationRuntimeQuery } from '@zlink-systems/framework';
import { ZLINK_LOCATION_RUNTIME_QUERY, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { validateServerOptions } from './Configuration/server-options';
import type { ServerOptions } from './Configuration/server-options';
import { RESILIENCE_OPTIONS, createResilienceConfigurationModule } from '../../configuration';
import { createTopologyProbeEndpoints } from './Endpoints/topology-probe-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { createRedisLocationStore, resilienceLocationOptions } from '../../Shared/location-store';

export async function startTopologyProbeHost(): Promise<void> {
  let stopping = false;
  const TopologyProbeModule = createTopologyProbeModule();
  const app = await NestFactory.createApplicationContext(TopologyProbeModule, { logger: false, abortOnError: false });
  const options = app.get(RESILIENCE_OPTIONS, { strict: false }) as ServerOptions;
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  const locationQuery = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  const server = await startHttpServer(
    options.httpUrl,
    createTopologyProbeEndpoints(options, locationQuery, evidence, () => { stopping = true; })
  );
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createTopologyProbeModule(): Function {
  class TopologyProbeModule {}
  const configuration = createResilienceConfigurationModule(validateServerOptions);
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration], inject: [RESILIENCE_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as ServerOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const builder = zlinkFramework();
          builder.addLocationStore(createRedisLocationStore({
            redisEndpoint: options.redisEndpoint,
            redisKeyPrefix: options.redisKeyPrefix
          }));
          Object.assign(builder.configureLocations(), resilienceLocationOptions());
          return builder.build();
        }
      })
    ]
  })(TopologyProbeModule);
  return TopologyProbeModule;
}
