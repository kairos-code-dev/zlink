import 'reflect-metadata';
import http from 'node:http';
import { URL } from 'node:url';
import { NestFactory } from '@nestjs/core';
import type { ZLinkLocationRuntimeQuery, ZLinkSpotHandleResolver, ZLinkSpotManager, ZLinkSpotOutbound } from '@zlink-systems/framework';
import { ZLINK_LOCATION_RUNTIME_QUERY, ZLINK_SPOT_HANDLE_RESOLVER, ZLINK_SPOT_MANAGER, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { loadSampleConfig, type ShoppingMallServerConfig } from './Configuration/sample-config';
import { createCommerceApiServer } from './CommerceApi/commerce-api-server';
import { createShoppingMallCommerceApiModule } from './CommerceApi/commerce-api-module';
import { OrderWorkflowRouterPort } from './CommerceApi/Application/order-workflow-router-port';
import { StartOrderUseCase } from './CommerceApi/Application/start-order-use-case';
import { createShoppingMallWorkflowModule } from './OrderWorkflow/shoppingmall-workflow-module';
import { OrderStore } from './Shared/Store/order-store';
import { SampleNames } from '../Shared/Configuration/sample-names';
import { ShoppingMallTopologyReadyReq } from '../Shared/Contracts/messages';
import { OrderWorkflowSpot } from './OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot';

const role = readArg('--role') ?? SampleNames.apiA;
const config = loadSampleConfig();
const endpoint = endpointForRole(role, config);
const listenUrl = new URL(endpoint);

async function main(): Promise<void> {
  const moduleType = isWorkflowRole(role)
    ? createShoppingMallWorkflowModule(role, config)
    : createShoppingMallCommerceApiModule(role, config);
  const app = await NestFactory.createApplicationContext(moduleType, {
    logger: false,
    abortOnError: false
  });
  const server = isWorkflowRole(role)
    ? createHealthServer(role, {
        locations: app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }),
        spots: app.get(ZLINK_SPOT_MANAGER, { strict: false }),
        spotRefs: app.get(ZLINK_SPOT_HANDLE_RESOLVER, { strict: false }),
        outbound: app.get(ZLINK_SPOT_OUTBOUND, { strict: false })
      })
    : createCommerceApiServer(
      endpoint,
      role,
      app.get(OrderStore, { strict: false }),
      app.get(StartOrderUseCase, { strict: false }),
      app.get(OrderWorkflowRouterPort, { strict: false })
    );

  server.listen(Number(listenUrl.port), listenUrl.hostname, () => {
    console.log(`shoppingmall ${role} listening ${endpoint}`);
  });
  process.on('SIGINT', () => {
    server.close(() => {
      void app.close().finally(() => process.exit(0));
    });
  });
}

function createHealthServer(roleName: string, dependencies: {
  locations: ZLinkLocationRuntimeQuery;
  spots: ZLinkSpotManager;
  spotRefs: ZLinkSpotHandleResolver;
  outbound: ZLinkSpotOutbound;
}): http.Server {
  return http.createServer(async (request, response) => {
    const url = new URL(request.url ?? '/', endpoint);
    if (request.method === 'GET' && url.pathname === '/health') {
      try {
        const status = await dependencies.locations.getStatus();
        const probeId = 'shoppingmall-topology-readiness';
        if (roleName === SampleNames.workflowA) {
          await dependencies.spots.getOrCreate(OrderWorkflowSpot, probeId, { orderId: probeId });
        }
        const handle = await dependencies.spotRefs.resolveSpotHandle(probeId);
        if (handle === undefined) throw new Error('Topology readiness spot was not resolved.');
        const probe = await dependencies.outbound.requestToSpot(handle, new ShoppingMallTopologyReadyReq(probeId))
          .timeout(SampleNames.requestTimeout)
          .submit<{ ready: true }>();
        const ready = status.storeHealthy && probe.ready;
        response.writeHead(ready ? 200 : 503, { 'content-type': 'application/json' });
        response.end(JSON.stringify({ ok: ready, role: roleName }));
      } catch (error) {
        response.writeHead(503, { 'content-type': 'application/json' });
        response.end(JSON.stringify({ ok: false, role: roleName, error: error instanceof Error ? error.message : String(error) }));
      }
      return;
    }
    response.writeHead(404, { 'content-type': 'application/json' });
    response.end(JSON.stringify({ error: `No route for ${request.method ?? 'GET'} ${url.pathname}` }));
  });
}

function endpointForRole(roleName: string, values: ShoppingMallServerConfig): string {
  if (roleName === SampleNames.apiA) {
    return values.apiAHttpUrl;
  }
  if (roleName === SampleNames.apiB) {
    return values.apiBHttpUrl;
  }
  if (roleName === SampleNames.workflowA) {
    return values.workflowAHttpUrl;
  }
  if (roleName === SampleNames.workflowB) {
    return values.workflowBHttpUrl;
  }
  throw new Error(`Unknown ShoppingMall role '${roleName}'.`);
}

function isWorkflowRole(roleName: string): boolean {
  return roleName === SampleNames.workflowA || roleName === SampleNames.workflowB;
}

function readArg(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
