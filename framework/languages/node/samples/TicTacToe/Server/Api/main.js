const http = require('node:http');
const { createChannelClient } = require('../../../shared/channel-runtime');
const { createZLinkNestRuntime } = require('../../../shared/nestjs-provider-runtime');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { CreateGameHttpHandler } = require('./Handlers/create-game-http-handler');
const { PacketNames, SampleNames } = require('../../Shared/Contracts/messages');

async function main() {
  const playClient = await createChannelClient({
    channelName: SampleNames.playChannel,
    peers: [process.env.TICTACTOE_PLAY_ENDPOINT]
  });
  const authenticate = new AuthenticatePlayerHandler();
  const createGame = new CreateGameHttpHandler(playClient);
  const apiRuntime = await createZLinkNestRuntime({
    channels: {
      [SampleNames.apiChannel]: {
        server: { bind: process.env.TICTACTOE_API_ENDPOINT },
        handlerGroups: ['api'],
        requestHandlers: [{
          packetName: PacketNames.authenticatePlayerReq,
          handler: { handle: (payload) => authenticate.handle(decodePayload(payload)) }
        }]
      }
    }
  });
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
  await playClient.stop();
  await apiRuntime.close();
}

function decodePayload(payload) {
  return JSON.parse(Buffer.from(payload).toString());
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
