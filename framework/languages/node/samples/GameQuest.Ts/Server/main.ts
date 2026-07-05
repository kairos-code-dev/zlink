import 'reflect-metadata';
import http from 'node:http';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createGameApiModule } from './GameApi/game-api-module';
import { startGameApiServer } from './GameApi/game-api-server';
import { createQuestMissionModule } from './QuestMission/gamequest-quest-module';
import { RegistryModule } from './Registry/registry-module';
import { QuestProgressStore } from './Shared/Store/quest-progress-store';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const role = readOption('--role') ?? process.env.GAMEQUEST_ROLE ?? 'all';
  const modules = [];
  if (role === 'all' || role === 'mission-a') {
    process.env.GAMEQUEST_MISSION_NAME = 'mission-a';
    modules.push(createQuestMissionModule(config, 'mission-a'));
  }
  if (role === 'all' || role === 'mission-b') {
    process.env.GAMEQUEST_MISSION_NAME = 'mission-b';
    modules.push(createQuestMissionModule(config, 'mission-b'));
  }
  if (role === 'all' || role === 'api-a') {
    process.env.GAMEQUEST_API_NAME = 'api-a';
    modules.push(createGameApiModule(config, 'api-a'));
  }
  if (role === 'all' || role === 'api-b') {
    process.env.GAMEQUEST_API_NAME = 'api-b';
    modules.push(createGameApiModule(config, 'api-b'));
  }
  if (modules.length === 0) {
    throw new Error(`Unknown GameQuest role '${role}'.`);
  }
  void RegistryModule;

  const apps = [];
  for (const moduleType of modules) {
    apps.push(await NestFactory.createApplicationContext(moduleType, {
      logger: false,
      abortOnError: false
    }));
  }

  const httpServers = [];
  if (role === 'all' || role === 'api-a') {
    httpServers.push(await startGameApiServer(apps[apps.length - 1], config, 'api-a'));
  }
  if (role === 'all' || role === 'api-b') {
    httpServers.push(await startGameApiServer(apps[apps.length - 1], config, 'api-b'));
  }
  if (role === 'all' || role === 'mission-a') {
    httpServers.push(await startMissionSelfCheckServer(config, 'mission-a'));
  }
  if (role === 'all' || role === 'mission-b') {
    httpServers.push(await startMissionSelfCheckServer(config, 'mission-b'));
  }

  process.stdout.write(`${JSON.stringify({ event: 'ready', role })}\n`);
  await waitForShutdown();
  for (const server of httpServers) {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
  for (const app of apps.reverse()) {
    await closeNestRuntime(app);
  }
}

function readOption(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : undefined;
}

async function closeNestRuntime(container: { close(): Promise<void> }): Promise<void> {
  try {
    await container.close();
  } catch (error) {
    const candidate = error as { name?: string; code?: number };
    if (candidate.name === 'CloseError' && (candidate.code === 0 || candidate.code === 401)) {
      return;
    }
    throw error;
  }
}

function waitForShutdown(): Promise<void> {
  return new Promise<void>((resolve) => {
    const keepAlive = setInterval(() => undefined, 1000);
    const stop = () => {
      clearInterval(keepAlive);
      resolve();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
}

function startMissionSelfCheckServer(config: ReturnType<typeof loadSampleConfig>, role: 'mission-a' | 'mission-b'): Promise<http.Server> {
  const store = new QuestProgressStore(config.workDir);
  const missionUrl = role === 'mission-a'
    ? process.env.GAMEQUEST_MISSION_A_HTTP ?? 'http://127.0.0.1:31213'
    : process.env.GAMEQUEST_MISSION_B_HTTP ?? 'http://127.0.0.1:31214';
  const url = new URL(missionUrl);
  const server = http.createServer((request, response) => {
    const ownerMatch = request.url?.match(/^\/self-check\/owner\/([^/]+)\/close$/);
    if (request.method === 'GET' && request.url === '/health') {
      sendJson(response, 200, { ready: true, role });
      return;
    }
    if (request.method === 'GET' && request.url === '/self-check/events') {
      sendJson(response, 200, store.readQuestEventNames());
      return;
    }
    if (request.method === 'POST' && ownerMatch !== undefined && ownerMatch !== null) {
      store.closeOwner(decodeURIComponent(ownerMatch[1]), role);
      sendJson(response, 200, { closed: true });
      return;
    }
    sendJson(response, 404, { error: 'not-found' });
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(Number(url.port), url.hostname, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

function sendJson(response: http.ServerResponse, statusCode: number, body: unknown): void {
  response.writeHead(statusCode, { 'content-type': 'application/json' });
  response.end(JSON.stringify(body));
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
