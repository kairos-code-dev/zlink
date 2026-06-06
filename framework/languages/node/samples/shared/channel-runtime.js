const { createZLinkNestRuntime, nestjs } = require('./nestjs-provider-runtime');
const {
  closeNestRuntime,
  decodePayload,
  retry: retryOperation,
  waitForShutdown
} = require('./runtime-common');

function createChannelServerOptions({ endpoint, channelName, handlers = [], handlerGroups }) {
  const exposedHandlers = exposeHandlerGroups(handlers, handlerGroups);
  return {
    clientServerChannels: {
      [channelName]: {
        server: { bind: endpoint },
        handlerGroups,
        requestHandlers: exposedHandlers.map(({ packetName, handle }) => ({
          packetName,
          handler: {
            async handle(payload, context) {
              return await handle(decodePayload(payload), context);
            }
          }
        }))
      }
    }
  };
}

function createChannelClientOptions({ channelName, peers }) {
  return {
    clientServerChannels: {
      [channelName]: { client: { manualConnections: peers } }
    }
  };
}

async function startChannelServer({ endpoint, channelName, handlers, handlerGroups, providers = [] }) {
  let container;
  const resolver = {
    get(token) {
      if (container === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return container.get(token);
    }
  };
  const resolvedHandlers = typeof handlers === 'function' ? handlers(resolver) : handlers;
  container = await createZLinkNestRuntime(
    createChannelServerOptions({ endpoint, channelName, handlers: resolvedHandlers, handlerGroups }),
    providers
  );
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint, channelName })}\n`);
  await waitForShutdown({ keepAlive: true });
  await closeNestRuntime(container);
}

function exposeHandlerGroups(handlers, handlerGroups) {
  if (handlerGroups === undefined) {
    return handlers;
  }

  const groups = new Set(handlerGroups);
  const exposed = handlers.filter((handler) => groups.has(handler.group));
  for (const group of groups) {
    if (handlers.length > 0 && !handlers.some((handler) => handler.group === group)) {
      throw new Error(`No sample handler is registered for group '${group}'.`);
    }
  }
  return exposed;
}

async function createChannelClient({ channelName, peers }) {
  const container = await createZLinkNestRuntime(createChannelClientOptions({ channelName, peers }));
  const client = container.get(nestjs.ZLINK_CHANNEL_CLIENT);
  return {
    requestToChannel(targetChannelName, payload) {
      return retryableRequestCall(() => client.requestToChannel(targetChannelName, payload));
    },
    async request(packetName, payload, timeoutMs = 1000) {
      return await this
        .requestToChannel(channelName, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit();
    },
    async stop() {
      await closeNestRuntime(container);
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
      return await retryOperation(() => {
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

function isTransientConnectError(error) {
  return error instanceof Error && (
    (error.code === 2 && /Host unreachable/.test(error.message)) ||
    /ZLink async submit timed out/.test(error.message)
  );
}

module.exports = { createChannelClient, startChannelServer };
