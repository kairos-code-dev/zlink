require('reflect-metadata');

const net = require('node:net');
const { NestFactory } = require('@nestjs/core');
const connector = require('../../../../../packages/stream-connector/dist');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createChannelClient, createRegistryClient } = require('../discovery-support');
const { BingoSession } = require('./Sessions/bingo-session');
const { createBingoSessionModule, getSessionAuthenticator } = require('./bingo-session-module');
const { SampleNames, SampleTimings } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { PacketNames, bingoNotificationsReq, withPlayerIdentity } = require('../../Shared/Contracts/messages');
const { fromBingoProto, toBingoProto } = require('../../Shared/Contracts/protobuf-codec');
import type { Server, Socket } from 'node:net';
import type { ZLinkChannelClient } from '../../../../packages/framework/dist';
import type { BingoChannelClient } from '../discovery-support';
import type { BingoSession as BingoSessionType } from './Sessions/bingo-session';
import type { BingoActorRef } from '../../Shared/Contracts/messages';

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
    bound: BingoSessionActorRef[];
    bind(actor: BingoActorRef): Promise<void>;
  };
};

type BingoSessionActorRef = BingoActorRef & {
  relay(header: StreamHeader, payload: unknown): Promise<unknown>;
};

type NotificationBatch = {
  nextSeq: number;
  delivered: Array<{ seq: number; packetName: string; payload: unknown }>;
};

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const registry = await createRegistryClient(config.registryEndpoint);
  const api = await registry.resolve(SampleNames.apiService);
  const play = await registry.resolve(SampleNames.playService);
  const notifications = await registry.resolve(SampleNames.notificationService);
  const notificationClient = await createChannelClient(SampleNames.notificationChannel, notifications.endpoint);
  const BingoSessionModule = createBingoSessionModule({
    apiEndpoint: api.endpoint,
    playEndpoint: play.endpoint
  });
  const app = await NestFactory.createApplicationContext(BingoSessionModule, {
    logger: false,
    abortOnError: false
  });
  const channelClient = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const authenticate = getSessionAuthenticator(app);

  const streamServer = net.createServer((socket: Socket) => {
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
      session.onDisconnected();
    });
    socket.on('data', (chunk: Buffer) => {
      buffer = Buffer.concat([buffer, chunk]);
      while (true) {
        const packet = tryReadFrame(buffer);
        if (packet === undefined) {
          return;
        }
        buffer = buffer.subarray(packet.length);
        void dispatchPacket(session, context, packet.bytes, transport, channelClient);
      }
    });
  });

  await listen(streamServer, config.sessionEndpoint);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.sessionEndpoint,
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

async function dispatchPacket(
  session: BingoSessionType,
  context: SessionContext,
  bytes: Buffer,
  transport: StreamTransport,
  channelClient: ZLinkChannelClient
): Promise<void> {
  try {
    const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
    const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
    const payload = fromBingoProto({ codec: header.codec, payload: frame.payload });
    if (header.name === PacketNames.authenticateReq) {
      await session.dispatch(header, payload);
      return;
    }
    const response = await relayToPlay(channelClient, context, header.name, payload);
    transport.reply(header, response);
  } catch (error) {
    transport.send('StreamError', { message: error instanceof Error ? error.message : String(error) });
  }
}

async function relayToPlay(
  channelClient: ZLinkChannelClient,
  context: SessionContext,
  packetName: string,
  request: object
): Promise<unknown> {
  return await relayToChannel(channelClient, SampleNames.playChannel, context, packetName, request);
}

async function relayToChannel(
  channelClient: ZLinkChannelClient | BingoChannelClient,
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
    .submit<unknown>();
}

async function pumpNotifications(
  channelClient: BingoChannelClient,
  context: SessionContext,
  transport: StreamTransport
): Promise<void> {
  while (!context.closed) {
    if (context.actorId !== null && context.displayName !== null) {
      try {
        const response = await relayToChannel(
          channelClient,
          SampleNames.notificationChannel,
          context,
          PacketNames.bingoNotificationsReq,
          bingoNotificationsReq(context.notificationCursor)
        ) as NotificationBatch;
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
      async bind(actor: BingoActorRef): Promise<void> {
        this.bound.push(createBoundActor(actor));
      }
    }
  };
}

class StreamTransport {
  constructor(private readonly socket: Socket) {}

  reply(requestHeader: StreamHeader, payload: unknown): void {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.Protobuf,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: requestHeader.name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }, payload);
  }

  send(packetName: string, payload: unknown, metadata: Record<string, string> = {}): void {
    this.write({
      kind: connector.ZlinkStreamMessageKind.Send,
      codec: connector.ZlinkStreamCodec.Protobuf,
      flags: Object.keys(metadata).length === 0
        ? connector.ZlinkStreamHeaderFlags.None
        : connector.ZlinkStreamHeaderFlags.HasMetadata,
      name: packetName,
      metadata: connector.ZlinkStreamMetadataMap.from(Object.entries(metadata))
    }, payload);
  }

  write(header: unknown, payload: unknown): void {
    const encoded = toBingoProto(payload, undefined, (header as { name?: string }).name);
    this.socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode(header),
      encoded.payload
    ));
  }
}

function createBoundActor(actor: BingoActorRef): BingoSessionActorRef {
  return {
    ...actor,
    async relay(header: StreamHeader, payload: unknown): Promise<unknown> {
      void header;
      return payload;
    }
  };
}

function listen(server: Server, endpoint: string): Promise<void> {
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
