require('reflect-metadata');

const net = require('node:net');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const connector = require('../../../../../packages/stream-connector/dist');
const msgpack = require('../../../../../packages/stream-connector-msgpack/dist');
const { ZLinkModule, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleNames } = require('../Configuration/sample-settings');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { PlayActorFactory } = require('./Adapters/ZLink/Actors/play-actor-factory');
const { PlayEntrySpot } = require('./Adapters/ZLink/Spots/play-entry-spot');
const { PlayActorJoinGameHandler } = require('./Adapters/ZLink/Spots/Handlers/play-actor-join-game-handler');
const { PlayActorPlaceMarkHandler } = require('./Adapters/ZLink/Spots/Handlers/play-actor-place-mark-handler');
const { TicTacToeGameTimerHandler } = require('./Adapters/ZLink/Spots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameCreator } = require('./Application/GameCreation/tictactoe-game-creator');
const { CreateGameHandler } = require('./Adapters/ZLink/Handlers/create-game-handler');
const { PlaySessionFactory } = require('./Adapters/ZLink/Sessions/play-session-factory');
const { PLAY_STREAM_ENDPOINT } = require('./play-tokens');

type TcpEndpoint = {
  host: string;
  port: number;
};

type ReadFrameResult = {
  length: number;
  bytes: Buffer;
};

type StreamHeader = {
  requestSeq?: number;
  name: string;
};

type DispatchSession = {
  dispatch(header: unknown, payload: unknown): Promise<void>;
};

async function main(): Promise<void> {
  const config = loadSampleConfig();
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .server(config.playEndpoint)
            .handlerGroup('play'))
          .build()
      })
    ],
    providers: [
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      TicTacToeGameCreator,
      PlayActorFactory,
      PlayActorJoinGameHandler,
      PlayEntrySpot,
      PlayActorPlaceMarkHandler,
      TicTacToeGameTimerHandler,
      PlaySessionFactory,
      CreateGameHandler
    ]
  })(TicTacToePlayModule);

  const channelApp = await NestFactory.createApplicationContext(TicTacToePlayModule, {
    logger: false,
    abortOnError: false
  });
  const sessionFactory = channelApp.get(PlaySessionFactory, { strict: false });

  const streamServer = net.createServer((socket: any) => {
    const transport = new StreamTransport(socket);
    const session = sessionFactory.create(transport);
    let buffer = Buffer.alloc(0);
    socket.on('error', () => {});
    socket.on('data', (chunk: Buffer) => {
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
  await listen(streamServer, config.playStreamEndpoint);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    streamEndpoint: config.playStreamEndpoint
  })}\n`);
  await waitForShutdown();
  await new Promise<void>((resolve, reject) => streamServer.close((error) => error ? reject(error) : resolve()));
  await closeNestRuntime(channelApp);
}

class StreamTransport {
  [key: string]: any;
  constructor(socket: any) {
    this.socket = socket;
  }

  reply(requestHeader: StreamHeader, payload: unknown): void {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.MessagePack,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: requestHeader.name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }, payload);
  }

  send(packetName: string, payload: unknown, metadata: Record<string, string> = {}): void {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Send,
      codec: connector.ZlinkStreamCodec.MessagePack,
      flags: Object.keys(metadata).length === 0
        ? connector.ZlinkStreamHeaderFlags.None
        : connector.ZlinkStreamHeaderFlags.HasMetadata,
      name: packetName,
      metadata: connector.ZlinkStreamMetadataMap.from(Object.entries(metadata))
    }, payload);
  }

  write(header: unknown, payload: unknown): void {
    this.socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode(header),
      msgpack.toMsgPack(payload).payload
    ));
  }
}

async function dispatchPacket(session: DispatchSession, bytes: Buffer, transport: StreamTransport): Promise<void> {
  try {
    const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
    const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
    await session.dispatch(header, msgpack.fromMsgPack({ codec: header.codec, payload: frame.payload }));
  } catch (error) {
    transport.send('StreamError', { message: error instanceof Error ? error.message : String(error) });
  }
}

function listen(server: any, endpoint: string): Promise<void> {
  const { host, port } = parseTcpEndpoint(endpoint);
  return new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
}

function parseTcpEndpoint(endpoint: string): TcpEndpoint {
  const value = endpoint.replace('tcp://', '');
  const [host, port] = value.split(':');
  return { host, port: Number(port) };
}

function tryReadFrame(buffer: Buffer): ReadFrameResult | undefined {
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

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
