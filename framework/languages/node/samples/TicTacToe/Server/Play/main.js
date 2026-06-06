require('reflect-metadata');

const net = require('node:net');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const connector = require('../../../../packages/stream-connector/dist');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime } = require('../../../shared/runtime-common');
const { PacketNames, SampleNames } = require('../../Shared/Contracts/messages');
const { PlayActorFactory } = require('./Actors/play-actor-factory');
const { PlayEntrySpot } = require('./EntrySpot/play-entry-spot');
const { PlayActorJoinGameHandler } = require('./EntrySpot/Handlers/play-actor-join-game-handler');
const { PlayActorPlaceMarkHandler } = require('./GameSpots/Handlers/play-actor-place-mark-handler');
const { TicTacToeGameCreatedHandler } = require('./GameSpots/Handlers/tictactoe-game-created-handler');
const { TicTacToeGameJoinHandler } = require('./GameSpots/Handlers/tictactoe-game-join-handler');
const { TicTacToeGameTimerHandler } = require('./GameSpots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameDirectory } = require('./GameSpots/tictactoe-game');
const { CreateGameHandler } = require('./Handlers/create-game-handler');
const { PlaySessionFactory } = require('./Sessions/play-session-factory');
const { PLAY_STREAM_ENDPOINT } = require('./play-tokens');

const encoder = new TextEncoder();
const decoder = new TextDecoder();

async function main() {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        clientServerChannels: {
          [SampleNames.playChannel]: {
            server: { bind: process.env.TICTACTOE_PLAY_ENDPOINT },
            handlerGroups: ['play']
          }
        }
      })
    ],
    providers: [
      { provide: PLAY_STREAM_ENDPOINT, useValue: process.env.TICTACTOE_PLAY_STREAM_ENDPOINT },
      TicTacToeGameDirectory,
      PlayActorFactory,
      TicTacToeGameJoinHandler,
      PlayActorJoinGameHandler,
      PlayEntrySpot,
      PlayActorPlaceMarkHandler,
      TicTacToeGameCreatedHandler,
      TicTacToeGameTimerHandler,
      PlaySessionFactory,
      ...zlinkHandlerGroup('play', [[CreateGameHandler, PacketNames.createGame]])
    ]
  })(TicTacToePlayModule);

  const channelApp = await NestFactory.createApplicationContext(TicTacToePlayModule, {
    logger: false,
    abortOnError: false
  });
  const sessionFactory = channelApp.get(PlaySessionFactory, { strict: false });

  const streamServer = net.createServer((socket) => {
    const transport = new StreamTransport(socket);
    const session = sessionFactory.create(transport);
    let buffer = Buffer.alloc(0);
    socket.on('error', () => {});
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      while (true) {
        const packet = tryReadFrame(buffer);
        if (packet === undefined) {
          return;
        }
        buffer = buffer.subarray(packet.length);
        void dispatchPacket(session, packet.bytes, transport);
      }
    });
  });
  await listen(streamServer, process.env.TICTACTOE_PLAY_STREAM_ENDPOINT);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_PLAY_ENDPOINT,
    streamEndpoint: process.env.TICTACTOE_PLAY_STREAM_ENDPOINT
  })}\n`);
  await waitForShutdown();
  await new Promise((resolve, reject) => streamServer.close((error) => error ? reject(error) : resolve()));
  await closeNestRuntime(channelApp);
}

class StreamTransport {
  constructor(socket) {
    this.socket = socket;
  }

  reply(requestHeader, payload) {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: requestHeader.name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }, payload);
  }

  send(packetName, payload, metadata = {}) {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Send,
      codec: connector.ZlinkStreamCodec.Json,
      flags: Object.keys(metadata).length === 0
        ? connector.ZlinkStreamHeaderFlags.None
        : connector.ZlinkStreamHeaderFlags.HasMetadata,
      name: packetName,
      metadata: connector.ZlinkStreamMetadataMap.from(Object.entries(metadata))
    }, payload);
  }

  write(header, payload) {
    this.socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode(header),
      encoder.encode(JSON.stringify(payload))
    ));
  }
}

async function dispatchPacket(session, bytes, transport) {
  try {
    const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
    const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
    await session.dispatch(header, JSON.parse(decoder.decode(frame.payload)));
  } catch (error) {
    transport.send('StreamError', { message: error instanceof Error ? error.message : String(error) });
  }
}

function listen(server, endpoint) {
  const { host, port } = parseTcpEndpoint(endpoint);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
}

function parseTcpEndpoint(endpoint) {
  const value = endpoint.replace('tcp://', '');
  const [host, port] = value.split(':');
  return { host, port: Number(port) };
}

function tryReadFrame(buffer) {
  if (buffer.length < 6) {
    return undefined;
  }
  const headerLength = (buffer[0] << 8) | buffer[1];
  const payloadLength = buffer[2] * 0x1000000 + ((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
  const length = 6 + headerLength + payloadLength;
  if (buffer.length < length) {
    return undefined;
  }
  return { length, bytes: buffer.subarray(0, length) };
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
