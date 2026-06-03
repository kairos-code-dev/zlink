const { createZLinkNestRuntime, nestjs } = require('./nestjs-provider-runtime');

function createChannelServerOptions({ endpoint, channelName, handlers = [], handlerGroups }) {
  const exposedHandlers = exposeHandlerGroups(handlers, handlerGroups);
  return {
    channels: {
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
    channels: {
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
  await waitForShutdown();
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
      return await retry(() => {
        const call = createCall();
        if (packetNameValue !== undefined) {
          call.packetName(packetNameValue);
        }
        if (timeoutMs !== undefined) {
          call.timeout(timeoutMs);
        }
        return call.submit(signal);
      });
    }
  };
}

async function retry(action) {
  let lastError;
  for (let attempt = 0; attempt < 20; attempt += 1) {
    try {
      return await action();
    } catch (error) {
      if (!isTransientConnectError(error)) {
        throw error;
      }
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

function decodePayload(payload) {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    return JSON.parse(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return JSON.parse(payload);
  }
  return payload;
}

function isTransientConnectError(error) {
  return error instanceof Error && (
    (error.code === 2 && /Host unreachable/.test(error.message)) ||
    /ZLink async submit timed out/.test(error.message)
  );
}

function waitForShutdown() {
  return new Promise((resolve) => {
    const keepAlive = setInterval(() => {}, 60000);
    const stop = () => {
      clearInterval(keepAlive);
      resolve();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
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

module.exports = { createChannelClient, startChannelServer };
