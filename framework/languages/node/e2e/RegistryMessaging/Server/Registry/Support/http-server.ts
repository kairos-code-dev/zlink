import * as http from 'node:http';
import type { Server } from 'node:http';
import type { HttpRoute } from './types';

export async function startHttpServer(endpoint: string, routes: readonly HttpRoute[]): Promise<Server> {
  const server = http.createServer(async (request, response) => {
    const route = routes.find((candidate) => candidate.method === request.method && candidate.path === request.url);
    if (route === undefined) {
      response.writeHead(404).end();
      return;
    }
    try {
      const result = await route.handle();
      const payload = JSON.stringify(result ?? {}, jsonReplacer);
      response.writeHead(200, { 'content-type': 'application/json' });
      response.end(payload);
    } catch (error) {
      response.writeHead(500, { 'content-type': 'application/json' });
      response.end(JSON.stringify({ error: error instanceof Error ? error.message : String(error) }));
    }
  });
  const url = new URL(endpoint);
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, resolve);
  });
  return server;
}

function jsonReplacer(_key: string, value: unknown): unknown {
  return typeof value === 'bigint' ? value.toString() : value;
}

export function closeHttpServer(server: Server): Promise<void> {
  return new Promise<void>((resolve, reject) => server.close((error) => error === undefined ? resolve() : reject(error)));
}
