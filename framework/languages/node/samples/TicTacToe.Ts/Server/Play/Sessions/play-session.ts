require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const { PacketNames, SampleNames, SampleTimings } = require('../../../Shared/Contracts/messages');

class PlaySession {
  [key: string]: any;
  constructor(dependencies, transport) {
    this.dependencies = dependencies;
    this.transport = transport;
    this.actor = null;
  }

  async dispatch(header, payload) {
    if (header.name === PacketNames.authenticateReq) {
      await this.authenticate(header, payload);
      return;
    }
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before play packets.');
    }
    if (header.name === PacketNames.joinGameReq) {
      await this.joinGame(header, payload);
      return;
    }
    if (header.name === PacketNames.placeMarkReq) {
      await this.placeMark(header, payload);
      return;
    }
    throw new Error(`Unsupported play stream packet '${header.name}'.`);
  }

  async authenticate(header, request) {
    const apiClient = await createChannelClient({
      channelName: SampleNames.apiChannel,
      peers: [this.dependencies.apiEndpoint]
    });
    try {
      const authenticated = await apiClient
        .requestToChannel(SampleNames.apiChannel, { accessToken: request.accessToken })
        .packetName(PacketNames.authenticatePlayerReq)
        .timeout(SampleTimings.requestTimeout)
        .submit();
      this.actor = this.dependencies.actorFactory.ensure(authenticated.actorId);
      this.actor.displayName = authenticated.displayName;
      this.actor.session = this;
      this.transport.reply(header, {
        actorId: authenticated.actorId,
        displayName: authenticated.displayName
      });
    } finally {
      await apiClient.stop();
    }
  }

  async joinGame(header, request) {
    const result = this.dependencies.entrySpot.join(this.actor, request.gameId);
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async placeMark(header, request) {
    const result = this.dependencies.placeMarkHandler.handle({
      actor: this.actor,
      gameId: request.gameId,
      cell: request.cell
    });
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async flushAllNotifications() {
    for (const actor of this.dependencies.actorFactory.actors.values()) {
      if (actor.session !== null) {
        actor.session.flushNotifications(actor);
      }
    }
  }

  flushNotifications(actor) {
    while (actor.notifications.length > 0) {
      const notification = actor.notifications.shift();
      this.transport.send(notification.packetName, notification.payload, { seq: String(notification.seq) });
    }
  }
}

export { PlaySession };

async function createChannelClient({ channelName, peers }) {
  class PlaySessionChannelClientModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        clientServerChannels: {
          [channelName]: { client: { manualConnections: peers } }
        }
      })
    ]
  })(PlaySessionChannelClientModule);

  const app = await NestFactory.createApplicationContext(PlaySessionChannelClientModule, {
    logger: false,
    abortOnError: false
  });
  const client = app.get(ZLINK_CHANNEL_CLIENT, { strict: false });

  return {
    requestToChannel(targetChannelName, payload) {
      return retryableRequestCall(() => client.requestToChannel(targetChannelName, payload));
    },
    async stop() {
      await closeNestRuntime(app);
    }
  };
}

function retryableRequestCall(createCall) {
  let packetNameValue;
  let timeoutMs;
  return {
    packetName(packetName) {
      packetNameValue = packetName;
      return this;
    },
    timeout(value) {
      timeoutMs = value;
      return this;
    },
    async submit(signal) {
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

async function retry(action, options: any = {}) {
  const maxAttempts = options.maxAttempts ?? 5;
  const shouldRetry = options.shouldRetry ?? (() => true);
  let lastError;
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

function isTransientConnectError(error) {
  const candidate: any = error;
  return error instanceof Error && (
    (candidate.code === 2 && /Host unreachable/.test(error.message)) ||
    /ZLink async submit timed out/.test(error.message)
  );
}

async function closeNestRuntime(container) {
  try {
    await container.close();
  } catch (error) {
    if (error?.name === 'CloseError' && (error?.code === 0 || error?.code === 401)) {
      return;
    }
    throw error;
  }
}
