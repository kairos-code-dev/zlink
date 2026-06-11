require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkFramework } = require('../../../../../../../../packages/nestjs/dist');
const {
  PacketNames,
  authenticateReq,
  authenticateRes,
  joinGameInternalReq,
  placeMarkReq
} = require('../../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Configuration/sample-settings');
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  JoinGameReq,
  PlaceMarkStreamReq,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

type PlaySessionHeader = {
  name: string;
};

type PlaySessionTransport = {
  reply(header: PlaySessionHeader, payload: unknown): void;
  send(packetName: string, payload: unknown, metadata: Record<string, string>): void;
};

type PlaySessionDependencies = {
  apiEndpoint: string;
  actorFactory: {
    ensure(actorId: string): TicTacToeActor & { session: PlaySession | null; notifications: any[] };
    actors: Map<string, TicTacToeActor & { session: PlaySession | null; notifications: any[] }>;
  };
  entrySpot: {
    join(actor: TicTacToeActor, roomId: string): unknown;
  };
  placeMarkHandler: {
    handle(request: ReturnType<typeof placeMarkReq>): unknown;
  };
};

type RetryOptions = {
  maxAttempts?: number;
  shouldRetry?: (error: unknown) => boolean;
};

type RetryableCall = {
  packetName(packetName: string): RetryableCall;
  timeout(value: number): RetryableCall;
  submit(signal?: unknown): Promise<unknown>;
};

type ChannelClient = {
  requestToChannel(targetChannelName: string, payload: unknown): RetryableCall;
  stop(): Promise<void>;
};

class PlaySession {
  [key: string]: any;
  constructor(dependencies: PlaySessionDependencies, transport: PlaySessionTransport) {
    this.dependencies = dependencies;
    this.transport = transport;
    this.actor = null;
  }

  async dispatch(header: PlaySessionHeader, payload: unknown): Promise<void> {
    if (header.name === PacketNames.authenticateReq) {
      await this.authenticate(header, payload as AuthenticateReq);
      return;
    }
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before play packets.');
    }
    if (header.name === PacketNames.joinGameReq) {
      await this.joinGame(header, payload as JoinGameReq);
      return;
    }
    if (header.name === PacketNames.placeMarkReq) {
      await this.placeMark(header, payload as PlaceMarkStreamReq);
      return;
    }
    throw new Error(`Unsupported play stream packet '${header.name}'.`);
  }

  async authenticate(header: PlaySessionHeader, request: AuthenticateReq): Promise<void> {
    const apiClient = await createChannelClient({
      channelName: SampleNames.apiChannel,
      peers: [this.dependencies.apiEndpoint]
    });
    try {
      const authenticated = await apiClient
        .requestToChannel(SampleNames.apiChannel, authenticateReq(request.accessToken))
        .packetName(PacketNames.authenticatePlayerReq)
        .timeout(SampleTimings.requestTimeout)
        .submit() as AuthenticatePlayerRes;
      this.actor = this.dependencies.actorFactory.ensure(authenticated.actorId);
      this.actor.displayName = authenticated.displayName;
      this.actor.session = this;
      this.transport.reply(header, authenticateRes(authenticated.actorId, authenticated.displayName));
    } finally {
      await apiClient.stop();
    }
  }

  async joinGame(header: PlaySessionHeader, request: JoinGameReq): Promise<void> {
    const result = this.dependencies.entrySpot.join(this.actor, request.roomId);
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async placeMark(header: PlaySessionHeader, request: PlaceMarkStreamReq): Promise<void> {
    const result = this.dependencies.placeMarkHandler.handle(placeMarkReq(this.actor, request.cell));
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async flushAllNotifications(): Promise<void> {
    for (const actor of this.dependencies.actorFactory.actors.values()) {
      if (actor.session !== null) {
        actor.session.flushNotifications(actor);
      }
    }
  }

  flushNotifications(actor: TicTacToeActor & { notifications: any[] }): void {
    while (actor.notifications.length > 0) {
      const notification = actor.notifications.shift();
      this.transport.send(notification.packetName, notification.payload, { seq: String(notification.seq) });
    }
  }
}

export { PlaySession };

async function createChannelClient({ channelName, peers }: { channelName: string; peers: string[] }): Promise<ChannelClient> {
  class PlaySessionChannelClientModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .clientServerChannel(channelName, (channel) => channel.client(peers))
          .build()
      )
    ]
  })(PlaySessionChannelClientModule);

  const app = await NestFactory.createApplicationContext(PlaySessionChannelClientModule, {
    logger: false,
    abortOnError: false
  });
  const client = app.get(ZLINK_CHANNEL_CLIENT, { strict: false });

  return {
    requestToChannel(targetChannelName: string, payload: unknown): RetryableCall {
      return retryableRequestCall(() => client.requestToChannel(targetChannelName, payload));
    },
    async stop(): Promise<void> {
      await closeNestRuntime(app);
    }
  };
}

function retryableRequestCall(createCall: () => any): RetryableCall {
  let packetNameValue;
  let timeoutMs;
  return {
    packetName(packetName: string): RetryableCall {
      packetNameValue = packetName;
      return this;
    },
    timeout(value: number): RetryableCall {
      timeoutMs = value;
      return this;
    },
    async submit(signal?: unknown): Promise<unknown> {
      return await retry(() => {
        const call = createCall();
        if (packetNameValue !== undefined) {
          call.packetName(packetNameValue);
        }
        if (timeoutMs !== undefined) {
          call.timeout(timeoutMs);
        }
        return call.submit(signal);
      }, { maxAttempts: 20, shouldRetry: isTransientConnectError });
    }
  };
}

async function retry<TValue>(action: () => Promise<TValue>, options: RetryOptions = {}): Promise<TValue> {
  const maxAttempts = options.maxAttempts ?? 5;
  const shouldRetry = options.shouldRetry ?? (() => true);
  let lastError: unknown;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    try {
      return await action();
    } catch (error) {
      if (!shouldRetry(error)) {
        throw error;
      }
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

function isTransientConnectError(error: unknown): boolean {
  const candidate: any = error;
  return error instanceof Error && (
    (candidate.code === 2 && /Host unreachable/.test(error.message)) ||
    /ZLink async submit timed out/.test(error.message)
  );
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
