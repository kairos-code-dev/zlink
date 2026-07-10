import 'reflect-metadata';
import http from 'node:http';
import { NestFactory } from '@nestjs/core';
import { AuthenticateUserHandler } from './Handlers/authenticate-user-handler';
import { OpenConversationHandler } from './Handlers/open-conversation-handler';
import { createSupportChatApiModule } from './supportchat-api-module';
import { loadSampleConfig } from '../Configuration/sample-config';
import { closeServer, endpointPort, waitForShutdown } from '../runtime-support';
import { SupportChatStateStore } from '../Support/supportchat-state';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

void AuthenticateUserHandler;
void OpenConversationHandler;

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const app = await NestFactory.createApplicationContext(createSupportChatApiModule(config), {
    logger: false,
    abortOnError: false
  });
  const server = startHttpApi(config);
  process.stdout.write(`${JSON.stringify({ event: 'ready', role: 'api' })}\n`);
  await waitForShutdown();
  await closeServer(server);
  await app.close();
}

function startHttpApi(config: SupportChatServerConfig): http.Server {
  const store = new SupportChatStateStore(config.stateFile);
  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? '/', config.apiHttpUrl);
      if (req.method === 'GET' && url.pathname === '/health') {
        json(res, 200, { status: 'ready' });
        return;
      }
      if (req.method === 'POST' && url.pathname === '/auth') {
        const request = await body(req);
        json(res, 200, store.authenticate(String(request.accessToken)));
        return;
      }
      if (req.method === 'POST' && url.pathname === '/agent/available') {
        const request = await body(req);
        if (!String(request.actorId).startsWith('agent-')) {
          json(res, 400, { error: 'Customer must not set agent availability.' });
          return;
        }
        json(res, 200, { isAvailable: store.setAgentAvailable(String(request.actorId), Boolean(request.isAvailable)) });
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/open') {
        const request = await body(req);
        json(res, 200, store.openConversation(String(request.actorId), String(request.subject)));
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/join') {
        const request = await body(req);
        json(res, 200, { state: store.join(String(request.conversationId), String(request.actorId)) });
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/send') {
        const request = await body(req);
        json(res, 200, store.sendChat(String(request.conversationId), String(request.actorId), String(request.text)));
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/typing') {
        const request = await body(req);
        store.setTyping(String(request.conversationId), String(request.actorId), Boolean(request.isTyping));
        json(res, 200, { accepted: true });
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/close') {
        const request = await body(req);
        json(res, 200, { state: store.close(String(request.conversationId)) });
        return;
      }
      if (req.method === 'POST' && url.pathname === '/conversation/idle') {
        store.armIdle(String((await body(req)).conversationId));
        json(res, 200, { accepted: true });
        return;
      }
      if (req.method === 'GET' && url.pathname === '/notifications/take') {
        json(res, 200, store.takeNotification(
          String(url.searchParams.get('actorId')),
          String(url.searchParams.get('name')),
          url.searchParams.get('conversationId') ?? undefined
        ) ?? null);
        return;
      }
      if (req.method === 'GET' && url.pathname === '/evidence') {
        json(res, 200, { evidence: store.evidence() });
        return;
      }
      json(res, 404, { error: 'not found' });
    } catch (error) {
      json(res, 500, { error: error instanceof Error ? error.message : String(error) });
    }
  });
  server.listen(endpointPort(config.apiHttpUrl), '127.0.0.1');
  return server;
}

function body(req: http.IncomingMessage): Promise<Record<string, unknown>> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on('data', (chunk: Buffer) => chunks.push(chunk));
    req.on('end', () => {
      const text = Buffer.concat(chunks).toString('utf8');
      resolve(text.length === 0 ? {} : JSON.parse(text));
    });
    req.on('error', reject);
  });
}

function json(res: http.ServerResponse, status: number, value: unknown): void {
  res.writeHead(status, { 'content-type': 'application/json' });
  res.end(JSON.stringify(value));
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
