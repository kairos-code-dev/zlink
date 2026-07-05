import http from 'node:http';
import { URL } from 'node:url';
import { OrderStore } from './Shared/Store/order-store';

const role = readArg('--role') ?? 'api-a';
const endpoint = role === 'api-b'
  ? process.env.SHOPPINGMALL_API_B_HTTP
  : process.env.SHOPPINGMALL_API_A_HTTP;

if (endpoint === undefined) {
  throw new Error(`Missing HTTP endpoint for ${role}.`);
}

const listenUrl = new URL(endpoint);
const store = OrderStore.fromEnvironment();

const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url ?? '/', endpoint);
    if (request.method === 'GET' && url.pathname === '/health') {
      sendJson(response, 200, { ok: true, role });
      return;
    }
    if (request.method === 'POST' && url.pathname === '/orders/start') {
      sendJson(response, 200, store.startOrder(await readJson(request), role));
      return;
    }
    if (request.method === 'GET' && url.pathname.startsWith('/orders/')) {
      sendJson(response, 200, store.getOrder(decodeURIComponent(url.pathname.substring('/orders/'.length))));
      return;
    }
    if (request.method === 'POST' && url.pathname === '/self-check/idempotency/pending') {
      const body = await readJson(request);
      sendJson(response, 200, store.createPendingMapping(body.idempotencyKey, body.orderId, body.ownerInstanceId));
      return;
    }
    if (request.method === 'POST' && url.pathname === '/self-check/workflow/inventory-reserved') {
      sendJson(response, 200, store.createInventoryReserved(await readJson(request), role));
      return;
    }
    const continueMatch = url.pathname.match(/^\/self-check\/workflow\/([^/]+)\/continue$/);
    if (request.method === 'POST' && continueMatch !== null) {
      sendJson(response, 200, store.continueOrder(decodeURIComponent(continueMatch[1]), role));
      return;
    }
    const deleteMatch = url.pathname.match(/^\/self-check\/projection\/([^/]+)\/delete$/);
    if (request.method === 'POST' && deleteMatch !== null) {
      sendJson(response, 200, store.deleteProjection(decodeURIComponent(deleteMatch[1])));
      return;
    }
    const rebuildMatch = url.pathname.match(/^\/self-check\/projection\/([^/]+)\/rebuild$/);
    if (request.method === 'POST' && rebuildMatch !== null) {
      sendJson(response, 200, store.rebuildProjection(decodeURIComponent(rebuildMatch[1])));
      return;
    }
    if (request.method === 'POST' && url.pathname === '/self-check/assert') {
      sendJson(response, 200, store.assertEvidence(await readJson(request)));
      return;
    }
    sendJson(response, 404, { error: `No route for ${request.method ?? 'GET'} ${url.pathname}` });
  } catch (error) {
    sendJson(response, 500, { error: error instanceof Error ? error.message : String(error) });
  }
});

server.listen(Number(listenUrl.port), listenUrl.hostname, () => {
  console.log(`shoppingmall ${role} listening ${endpoint}`);
});

process.on('SIGINT', () => {
  server.close(() => process.exit(0));
});

function readArg(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

function readJson(request: http.IncomingMessage): Promise<any> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    request.on('data', (chunk: Buffer) => chunks.push(chunk));
    request.on('end', () => {
      const text = Buffer.concat(chunks).toString('utf8');
      resolve(text.length === 0 ? {} : JSON.parse(text));
    });
    request.on('error', reject);
  });
}

function sendJson(response: http.ServerResponse, status: number, body: unknown): void {
  response.writeHead(status, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}
