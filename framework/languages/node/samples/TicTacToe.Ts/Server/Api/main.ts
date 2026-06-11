require('reflect-metadata');

const http = require('node:http');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { CreateGameHttpHandler } = require('./Handlers/create-game-http-handler');
const { SampleNames } = require('../Configuration/sample-settings');
const { loadSampleConfig } = require('../Configuration/sample-config');

type HttpEndpoint = {
  host: string;
  port: number;
};

async function main(): Promise<void> {
  const config = loadSampleConfig();
  class TicTacToeApiModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .server(config.apiEndpoint)
            .handlerGroup('api'))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(config.playEndpoint))
          .build()
      })
    ],
    providers: [
      CreateGameHttpHandler,
      AuthenticatePlayerHandler
    ]
  })(TicTacToeApiModule);

  const apiApp = await NestFactory.createApplicationContext(TicTacToeApiModule, {
    logger: false,
    abortOnError: false
  });
  const createGame = apiApp.get(CreateGameHttpHandler, { strict: false });

  const server = http.createServer(async (request: any, response: any) => {
    if (request.method !== 'POST' || request.url !== '/games') {
      response.writeHead(404).end();
      return;
    }
    try {
      const body = await readJson(request);
      const result = await createGame.handle(body);
      response.writeHead(200, { 'content-type': 'application/json' });
      response.end(JSON.stringify(result));
    } catch (error) {
      response.writeHead(500, { 'content-type': 'application/json' });
      response.end(JSON.stringify({ error: error instanceof Error ? error.message : String(error) }));
    }
  });
  await listen(server, config.apiHttpEndpoint);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.apiEndpoint,
    httpEndpoint: config.apiHttpEndpoint
  })}\n`);
  await waitForShutdown();
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  await closeNestRuntime(apiApp);
}

function readJson(request: any): Promise<any> {
  return new Promise<any>((resolve, reject) => {
    const chunks: Buffer[] = [];
    request.on('data', (chunk: Buffer) => chunks.push(chunk));
    request.once('error', reject);
    request.once('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString()));
      } catch (error) {
        reject(error);
      }
    });
  });
}

function listen(server: any, endpoint: string): Promise<void> {
  const { host, port } = parseHttpEndpoint(endpoint);
  return new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
}

function parseHttpEndpoint(endpoint: string): HttpEndpoint {
  const url = new URL(endpoint);
  return { host: url.hostname, port: Number(url.port) };
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
