import 'reflect-metadata';
import * as net from 'node:net';
import { NestFactory } from '@nestjs/core';
import * as connector from '@zlink-systems/stream-connector';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { closeNestRuntime, retry, waitForShutdown } from '../runtime-support';
import { SupportChatSession } from './Sessions/supportchat-session';
import { createSupportChatSessionModule, getSessionAuthenticator } from './supportchat-session-module';
import { SampleNames } from '../Configuration/sample-names';
import { loadSampleConfig } from '../Configuration/sample-config';
import { PacketNames, supportNotificationsReq, withUserIdentity } from '../../Shared/Contracts/messages';
import type { Server, Socket } from 'node:net';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { SupportChatSession as SupportChatSessionType } from './Sessions/supportchat-session';
import type {
  AuthenticateReq,
  SupportNotificationBatch
} from '../../Shared/Contracts/messages';

type TcpEndpoint = {
  host: string;
  port: number;
};

type ReadFrameResult = {
  length: number;
  bytes: Buffer;
};

type StreamHeader = {
  requestSeq?: bigint;
  name: string;
};

type SessionContext = {
  actorId: string | null;
  displayName: string | null;
  role: string | null;
  notificationCursor: number;
  closed: boolean;
};

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const SupportChatSessionModule = createSupportChatSessionModule(config);
  const app = await NestFactory.createApplicationContext(SupportChatSessionModule, {
    logger: false,
    abortOnError: false
  });
  const channelClient = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const authenticate = getSessionAuthenticator(app);

  const streamServer = net.createServer((socket: Socket) => {
    const transport = new StreamTransport(socket);
    const context = createSessionContext();
    const session = new SupportChatSession(context, {
      async tryHandle(_sessionContext: SessionContext, header: StreamHeader, payload: unknown): Promise<boolean> {
        if (header.name !== PacketNames.authenticateReq) {
          return false;
        }
        const response = await authenticate.handle(payload as AuthenticateReq, context);
        transport.reply(header, response);
        void pumpNotifications(channelClient, context, transport);
        return true;
      },
      async relay(_sessionContext: SessionContext, header: StreamHeader, payload: unknown): Promise<unknown> {
        return await relayToSupport(channelClient, context, header.name, payload as object);
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
      for (;;) {
        const packet = tryReadFrame(buffer);
        if (packet === undefined) {
          return;
        }
        buffer = buffer.subarray(packet.length);
        void dispatchPacket(session, packet.bytes, transport);
      }
    });
  });

  await listen(streamServer, config.sessionEndpoint);
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.sessionEndpoint,
    stream: SupportChatSession.name
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await new Promise<void>((resolve, reject) => streamServer.close((error) => error === undefined ? resolve() : reject(error)));
    await closeNestRuntime(app);
  }
}

async function dispatchPacket(
  session: SupportChatSessionType,
  bytes: Buffer,
  transport: StreamTransport
): Promise<void> {
  let header: StreamHeader | undefined;
  try {
    const frame = connector.ZlinkStreamFrameCodec.decode(bytes);
    header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
    const payload = JSON.parse(Buffer.from(frame.payload).toString('utf8'));
    const response = await session.dispatch(header, payload);
    if (response !== undefined) {
      transport.reply(header, response);
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    if (header !== undefined) {
      transport.replyError(header, message);
      return;
    }
    transport.send('StreamError', { error: message });
  }
}

async function relayToSupport(
  channelClient: ZLinkChannelClient,
  context: SessionContext,
  packetName: string,
  request: object
): Promise<unknown> {
  return await relayToChannel(channelClient, SampleNames.supportChannel, context, packetName, request);
}

async function relayToChannel(
  channelClient: ZLinkChannelClient,
  channelName: string,
  context: SessionContext,
  packetName: string,
  request: object
): Promise<unknown> {
  if (context.actorId === null || context.displayName === null) {
    throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
  }
  const payload = withUserIdentity(request, context.actorId, context.displayName);
  return await retry(() => channelClient
      .requestToChannel(channelName, payload)
      .packetName(packetName)
      .submit<unknown>(), { delayMs: 25, maxAttempts: 200 });
}

async function pumpNotifications(
  channelClient: ZLinkChannelClient,
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
          PacketNames.supportNotificationsReq,
          supportNotificationsReq(context.notificationCursor)
        ) as SupportNotificationBatch;
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
    role: null,
    notificationCursor: 0,
    closed: false
  };
}

class StreamTransport {
  constructor(private readonly socket: Socket) {}

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

  replyError(requestHeader: StreamHeader, message: string): void {
    // The Session relays a rejected request back as a normal Response carrying an `error`
    // field. The client treats any reply with an `error` field as a failed request.
    this.write({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: requestHeader.name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }, { error: message });
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

  private write(header: connector.ZlinkStreamHeader, payload: unknown): void {
    this.socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode(header),
      new TextEncoder().encode(JSON.stringify(payload ?? null))
    ));
  }
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
