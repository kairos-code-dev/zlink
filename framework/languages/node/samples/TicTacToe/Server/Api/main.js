require('reflect-metadata');

const http = require('node:http');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime } = require('../../../shared/runtime-common');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { CreateGameHttpHandler } = require('./Handlers/create-game-http-handler');
const { PacketNames, SampleNames } = require('../../Shared/Contracts/messages');

async function main() {
  class TicTacToeApiModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        clientServerChannels: {
          [SampleNames.apiChannel]: {
            server: { bind: process.env.TICTACTOE_API_ENDPOINT },
            handlerGroups: ['api']
          },
          [SampleNames.playChannel]: {
            client: { manualConnections: [process.env.TICTACTOE_PLAY_ENDPOINT] }
          }
        }
      })
    ],
    providers: [
      CreateGameHttpHandler,
      ...zlinkHandlerGroup('api', [[AuthenticatePlayerHandler, PacketNames.authenticatePlayerReq]])
    ]
  })(TicTacToeApiModule);

  const apiApp = await NestFactory.createApplicationContext(TicTacToeApiModule, {
    logger: false,
    abortOnError: false
  });
  const createGame = apiApp.get(CreateGameHttpHandler, { strict: false });

  const server = http.createServer(async (request, response) => {
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
  await listen(server, process.env.TICTACTOE_API_HTTP_ENDPOINT);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_API_ENDPOINT,
    httpEndpoint: process.env.TICTACTOE_API_HTTP_ENDPOINT
  })}\n`);
  await waitForShutdown();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  await closeNestRuntime(apiApp);
}

function readJson(request) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    request.on('data', (chunk) => chunks.push(chunk));
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

function listen(server, endpoint) {
  const { host, port } = parseHttpEndpoint(endpoint);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
}

function parseHttpEndpoint(endpoint) {
  const url = new URL(endpoint);
  return { host: url.hostname, port: Number(url.port) };
}

function waitForShutdown() {
  return new Promise((resolve) => {
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
