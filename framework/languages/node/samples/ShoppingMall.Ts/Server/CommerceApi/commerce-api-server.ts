import http from 'node:http';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import {
  rebuildOrderProjectionReq,
  seedPendingIdempotencyReq,
  serverAssertionReq
} from '../../Shared/Contracts/messages';
import { StartOrderUseCase } from './Application/start-order-use-case';
import { ZLinkOrderWorkflowRouter } from './Infrastructure/ZLink/zlink-order-workflow-router';
import { OrderStore } from '../Shared/Store/order-store';
import type { INestApplicationContext } from '@nestjs/common';
import type { ZLinkRouteClient } from '@zlink-systems/framework';
import type {
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes
} from '../../Shared/Contracts/messages';
import type { SampleConfig } from '../Configuration/sample-config';

function startCommerceApi(
  app: INestApplicationContext,
  config: SampleConfig,
  instanceId: 'api-a' | 'api-b'
): Promise<http.Server> {
  const routes = app.get(ZLINK_ROUTE_CLIENT, { strict: false }) as ZLinkRouteClient;
  const orders = app.get(OrderStore, { strict: false });
  const workflow = new ZLinkOrderWorkflowRouter(routes);
  const startOrder = new StartOrderUseCase(orders, workflow);
  const server = http.createServer(async (request, response) => {
    try {
      const url = new URL(request.url ?? '/', 'http://127.0.0.1');
      if (request.method === 'GET' && url.pathname === '/health') {
        sendJson(response, 200, { ready: true, role: instanceId });
        return;
      }
      if (request.method === 'POST' && url.pathname === '/orders/start') {
        const body = await readJson<StartOrderReq>(request);
        const started = await startOrder.execute(body);
        console.error(`shoppingmall ${instanceId}: start order=${started.orderId}`);
        sendJson(response, 200, started satisfies StartOrderRes);
        return;
      }
      const orderMatch = /^\/orders\/([^/]+)$/.exec(url.pathname);
      if (request.method === 'GET' && orderMatch !== null) {
        sendJson(response, 200, orders.getOrder(decodeURIComponent(orderMatch[1])));
        return;
      }
      const deleteMatch = /^\/self-check\/projection\/([^/]+)\/delete$/.exec(url.pathname);
      if (request.method === 'POST' && deleteMatch !== null) {
        sendJson(response, 200, orders.deleteProjection(decodeURIComponent(deleteMatch[1])));
        return;
      }
      const rebuildMatch = /^\/self-check\/projection\/([^/]+)\/rebuild$/.exec(url.pathname);
      if (request.method === 'POST' && rebuildMatch !== null) {
        const rebuilt = await workflow.rebuildProjection(rebuildOrderProjectionReq(decodeURIComponent(rebuildMatch[1])));
        sendJson(response, 200, rebuilt);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/self-check/idempotency/pending') {
        const body = await readJson<Omit<SeedPendingIdempotencyReq, 'packetName'>>(request);
        const seeded = await workflow.seedPendingIdempotency(
          seedPendingIdempotencyReq(body.idempotencyKey, body.orderId, body.ownerInstanceId)
        );
        sendJson(response, 200, seeded);
        return;
      }
      if (request.method === 'POST' && url.pathname === '/self-check/assert') {
        const body = await readJson<ServerAssertionReq>(request);
        const assertion = orders.assertEvidence(
          body.successfulOrderId,
          body.pendingRecoveredOrderId,
          body.inventoryFailureOrderId,
          body.paymentFailureOrderId,
          body.scaleOutOrderId
        ) satisfies ServerAssertionRes;
        sendJson(response, 200, assertion);
        return;
      }
      sendJson(response, 404, { error: 'not-found' });
    } catch (error) {
      sendJson(response, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  const url = new URL(instanceId === 'api-a' ? config.apiAHttpUrl : config.apiBHttpUrl);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

function readJson<T>(request: http.IncomingMessage): Promise<T> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    request.on('data', (chunk) => chunks.push(Buffer.from(chunk)));
    request.on('error', reject);
    request.on('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')) as T);
      } catch (error) {
        reject(error);
      }
    });
  });
}

function sendJson(response: http.ServerResponse, statusCode: number, body: unknown): void {
  response.writeHead(statusCode, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}

export {
  startCommerceApi
};
