import http from 'node:http';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { assignDelivery } from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { retry } from '../Configuration/request-retry';
import type { INestApplicationContext } from '@nestjs/common';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  AssignDeliveryResult,
  CreateDeliveryRequest,
  DeliveryCreated,
  ServerAssertionReq,
  ServerAssertionRes
} from '../../Shared/Contracts/messages';
import type { EvidenceStore } from '../Configuration/evidence-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

function startDispatchApi(
  app: INestApplicationContext,
  config: DeliveryDispatchServerConfig,
  evidence: EvidenceStore
): Promise<http.Server> {
  const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = http.createServer(async (request, response) => {
    try {
      if (request.method === 'GET' && request.url === '/health') {
        sendJson(response, 200, { ready: true, role: 'dispatch-api' });
        return;
      }
      if (request.method === 'POST' && request.url === '/deliveries') {
        const body = await readJson<CreateDeliveryRequest>(request);
        const assigned = await requestDispatch(channels, body);
        if (!assigned.accepted) {
          throw new Error(`Dispatch Center rejected delivery '${assigned.deliveryId}'.`);
        }
        console.error(`deliverydispatch api: created delivery=${assigned.deliveryId} courier=${assigned.courierId}`);
        sendJson(response, 200, { deliveryId: assigned.deliveryId } satisfies DeliveryCreated);
        return;
      }
      if (request.method === 'POST' && request.url === '/self-check/assert') {
        const body = await readJson<ServerAssertionReq>(request);
        const success = evidence.hasSequence(body.successfulDeliveryId, 'Assigned', 'Accepted', 'PickedUp', 'Delivered');
        const reassigned = evidence.hasSequence(body.reassignedDeliveryId, 'Assigned', 'Reassigned', 'Accepted', 'PickedUp', 'Delivered');
        sendJson(response, 200, { passed: success && reassigned, evidence: evidence.readLines() } satisfies ServerAssertionRes);
        return;
      }
      sendJson(response, 404, { error: 'not-found' });
    } catch (error) {
      sendJson(response, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });

  const url = new URL(config.dispatchApiHttpUrl);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

async function requestDispatch(
  channels: ZLinkChannelClient,
  request: CreateDeliveryRequest
): Promise<AssignDeliveryResult> {
  return await retry(() => channels
    .requestToChannel(
      SampleNames.dispatchChannel,
      assignDelivery(request.deliveryId, request.customerId, request.pickupAddress, request.dropoffAddress))
    .submit<AssignDeliveryResult>(), { delayMs: 250, maxAttempts: 40 });
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

export { startDispatchApi };
