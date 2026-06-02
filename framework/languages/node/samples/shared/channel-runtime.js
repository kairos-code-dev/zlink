const framework = require('../../packages/framework/dist');

function createChannelServerRegistration({ endpoint, channelName, handlers = [] }) {
  return framework.createFrameworkRegistration({
    channels: {
      [channelName]: {
        server: { bind: endpoint },
        requestHandlers: handlers.map(({ packetName, handle }) => ({
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

async function startChannelServer({ endpoint, channelName, handlers, beforeReady }) {
  const registration = createChannelServerRegistration({ endpoint, channelName, handlers });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  await beforeReady?.();
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint, channelName })}\n`);
  await waitForShutdown();
  await stopRuntime(runtime);
}

async function createChannelClient({ channelName, peers }) {
  const registration = createChannelClientRegistration({ channelName, peers });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration });
  await runtime.start();
  const client = new framework.DefaultZLinkChannelClient(registration, runtime.channelTransport);
  return {
    async request(packetName, payload, timeoutMs = 1000) {
      return await retry(() => client
        .requestToChannel(channelName, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit());
    },
    async stop() {
      await stopRuntime(runtime);
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
