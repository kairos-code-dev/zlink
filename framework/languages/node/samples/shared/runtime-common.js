function decodePayload(payload) {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    return JSON.parse(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return JSON.parse(payload);
  }
  return payload;
}

async function retry(action, options = {}) {
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

function waitForShutdown(options = {}) {
  return new Promise((resolve) => {
    const keepAlive = options.keepAlive === true
      ? setInterval(() => {}, 60000)
      : undefined;
    const stop = () => {
      if (keepAlive !== undefined) {
        clearInterval(keepAlive);
      }
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

module.exports = {
  closeNestRuntime,
  decodePayload,
  retry,
  waitForShutdown
};
