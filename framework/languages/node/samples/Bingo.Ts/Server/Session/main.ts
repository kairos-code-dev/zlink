require('reflect-metadata');

const net = require('node:net');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const connector = require('../../../../../packages/stream-connector/dist');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createChannelClient, createRegistryClient } = require('../discovery-support');
const { AuthenticateSessionHandler } = require('./Sessions/Handlers/authenticate-session-handler');
const { BingoSession } = require('./Sessions/bingo-session');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames, bingoNotificationsReq, withPlayerIdentity } = require('../../Shared/Contracts/messages');

const encoder = new TextEncoder();
const decoder = new TextDecoder();

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

type SessionContext = {
  actorId: string | null;
  displayName: string | null;
  notificationCursor: number;
  closed: boolean;
  actors: {
    bound: any[];
    bind(actor: any): Promise<void>;
  };
};

async function bootstrap(): Promise<void> {
  const registry = await createRegistryClient(process.env.BINGO_REGISTRY_ENDPOINT);
  const api = await registry.resolve(SampleNames.apiService);
  const play = await registry.resolve(SampleNames.playService);
  const notifications = await registry.resolve(SampleNames.notificationService);
  const notificationClient = await createChannelClient(SampleNames.notificationChannel, notifications.endpoint);

  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .client(api.endpoint))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(play.endpoint))
          .build()
      )
    ],
    providers: [
      AuthenticateSessionHandler
    ]
  })(BingoSessionModule);

  const app = await NestFactory.createApplicationContext(BingoSessionModule, {
    logger: false,
    abortOnError: false
  });
  const channelClient = app.get(ZLINK_CHANNEL_CLIENT, { strict: false });
  const authenticate = app.get(AuthenticateSessionHandler, { strict: false });

  const streamServer = net.createServer((socket: any) => {
    const transport = new StreamTransport(socket);
    const context = createSessionContext();
    const session = new BingoSession(context, {
      async tryHandle(_sessionContext: SessionContext, header: StreamHeader, payload: unknown): Promise<boolean> {
        if (header.name !== PacketNames.authenticateReq) {
          return false;
        }
        const response = await authenticate.handle(payload, context);
        transport.reply(header, response);
        void pumpNotifications(notificationClient, context, transport);
        return true;
      }
    });
    let buffer = Buffer.alloc(0);
    socket.on('error', () => {});
    socket.once('close', () => {
      context.closed = true;
    });
    socket.on('data', (chunk: Buffer) => {
      buffer = Buffer.concat([buffer, chunk]);
      while (true) {
        const packet = tryReadFrame(buffer);
        if (packet === undefined) {
          return;
        }
        buffer = buffer.subarray(packet.length);
        void dispatchPacket(session, packet.bytes, transport, channelClient);
      }
    });
  });

  await listen(streamServer, process.env.BINGO_SESSION_ENDPOINT);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_SESSION_ENDPOINT,
    stream: BingoSession.name
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await new Promise<void>((resolve, reject) => streamServer.close((error) => error ? reject(error) : resolve()));
    await notificationClient.stop();
    await registry.stop();
    await closeNestRuntime(app);
  }
}

async function dispatchPacket(session: any, bytes: Buffer, transport: any, channelClient: any): Promise<void> {
  try {
    const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
    const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
    const payload = JSON.parse(decoder.decode(frame.payload));
    if (header.name === PacketNames.authenticateReq) {
      await session.dispatch(header, payload);
      return;
    }
    const response = await relayToPlay(channelClient, session.context, header.name, payload);
    transport.reply(header, response);
  } catch (error) {
    transport.send('StreamError', { message: error instanceof Error ? error.message : String(error) });
  }
}

async function relayToPlay(channelClient: any, context: SessionContext, packetName: string, request: object): Promise<unknown> {
  return await relayToChannel(channelClient, SampleNames.playChannel, context, packetName, request);
}

async function relayToChannel(
  channelClient: any,
  channelName: string,
  context: SessionContext,
  packetName: string,
  request: object
): Promise<unknown> {
  if (context.actorId === null || context.displayName === null) {
    throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
  }
  const payload = withPlayerIdentity(request, context.actorId, context.displayName);
  return await channelClient
    .requestToChannel(channelName, payload)
    .packetName(packetName)
    .timeout(SampleTimings.requestTimeout)
    .submit();
}

async function pumpNotifications(channelClient: any, context: SessionContext, transport: any): Promise<void> {
  while (!context.closed) {
    if (context.actorId !== null && context.displayName !== null) {
      try {
        const response = await relayToChannel(
          channelClient,
          SampleNames.notificationChannel,
          context,
          PacketNames.bingoNotificationsReq,
          bingoNotificationsReq(context.notificationCursor)
        ) as { nextSeq: number; delivered: Array<{ seq: number; packetName: string; payload: unknown }> };
        context.notificationCursor = response.nextSeq;
        for (const delivered of response.delivered) {
          transport.send(delivered.packetName, delivered.payload, { seq: String(delivered.seq) });
        }
      } catch (error) {
        if (context.closed) {
          return;
        }
      }
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
}

function createSessionContext(): SessionContext {
  return {
    actorId: null,
    displayName: null,
    notificationCursor: 0,
    closed: false,
    actors: {
      bound: [],
      async bind(actor: any): Promise<void> {
        this.bound.push(actor);
      }
    }
  };
}

class StreamTransport {
  [key: string]: any;
  constructor(socket: any) {
    this.socket = socket;
  }

  reply(requestHeader: StreamHeader, payload: unknown): void {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: requestHeader.name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }, payload);
  }

  send(packetName: string, payload: unknown, metadata: Record<string, string> = {}): void {
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

  write(header: unknown, payload: unknown): void {
    this.socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode(header),
      encoder.encode(JSON.stringify(payload))
    ));
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

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
