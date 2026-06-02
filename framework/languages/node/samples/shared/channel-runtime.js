const framework = require('../../packages/framework/dist');

function createChannelServerRegistration({ endpoint, channelName, handlers = [], handlerGroups }) {
  const exposedHandlers = exposeHandlerGroups(handlers, handlerGroups);
  return framework.createFrameworkRegistration({
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
  });
}

function createChannelClientRegistration({ channelName, peers }) {
  return framework.createFrameworkRegistration({
    channels: {
      [channelName]: { client: { manualConnections: peers } }
    }
  });
}

async function startChannelServer({ endpoint, channelName, handlers, handlerGroups, beforeReady }) {
  const registration = createChannelServerRegistration({ endpoint, channelName, handlers, handlerGroups });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  await beforeReady?.();
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint, channelName })}\n`);
  await waitForShutdown();
  await stopRuntime(runtime);
}

function exposeHandlerGroups(handlers, handlerGroups) {
  if (handlerGroups === undefined) {
    return handlers;
  }

  const groups = new Set(handlerGroups);
  const exposed = handlers.filter((handler) => groups.has(handler.group));
  for (const group of groups) {
    if (!handlers.some((handler) => handler.group === group)) {
      throw new Error(`No sample handler is registered for group '${group}'.`);
    }
  }
  return exposed;
}

async function createChannelClient({ channelName, peers }) {
  const registration = createChannelClientRegistration({ channelName, peers });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  const client = new framework.DefaultZLinkChannelClient(registration, runtime.channelTransport);
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
      await stopRuntime(runtime);
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
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
}

async function stopRuntime(runtime) {
  try {
    await runtime.stop();
  } catch (error) {
    if (error?.name === 'CloseError' && (error?.code === 0 || error?.code === 401)) {
      return;
    }
    throw error;
  }
}

module.exports = { createChannelClient, startChannelServer };
