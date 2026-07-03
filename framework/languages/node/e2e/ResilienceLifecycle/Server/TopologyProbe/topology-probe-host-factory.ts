import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { IZLinkLocationRuntimeQuery } from '@zlink-systems/framework';
import { ZLINK_LOCATION_RUNTIME_QUERY, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { parseServerOptions } from './Configuration/server-options';
import type { ServerOptions } from './Configuration/server-options';
import { createTopologyProbeEndpoints } from './Endpoints/topology-probe-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { createRedisLocationStore, resilienceLocationOptions } from '../../Shared/location-store';

export async function startTopologyProbeHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;
  const TopologyProbeModule = createTopologyProbeModule(options);
  const app = await NestFactory.createApplicationContext(TopologyProbeModule, { logger: false, abortOnError: false });
  const locationQuery = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as IZLinkLocationRuntimeQuery;
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

function createTopologyProbeModule(options: ServerOptions): Function {
  class TopologyProbeModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
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
