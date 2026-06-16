type ShutdownOptions = {
  keepAlive?: boolean;
};

type RetryOptions = {
  delayMs?: number;
  maxAttempts?: number;
};

function waitForShutdown(options: ShutdownOptions = {}): Promise<void> {
  return new Promise((resolve) => {
    const keepAlive = options.keepAlive === true
      ? setInterval(() => {}, 60000)
      : undefined;
    const stop = (): void => {
      if (keepAlive !== undefined) {
        clearInterval(keepAlive);
      }
      resolve(undefined);
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
}

async function retry<TValue>(action: () => Promise<TValue>, options: RetryOptions = {}): Promise<TValue> {
  const maxAttempts = options.maxAttempts ?? 100;
  const delayMs = options.delayMs ?? 0;
  let lastError: unknown;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    try {
      return await action();
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => {
        if (delayMs > 0) {
          setTimeout(resolve, delayMs);
          return;
        }
        setImmediate(resolve);
      });
    }
  }
  throw lastError;
}

async function closeNestRuntime(container: { close(): Promise<void> }): Promise<void> {
  try {
    await container.close();
  } catch (error) {
    const candidate = error as { name?: string; code?: number };
    if (candidate.name === 'CloseError' && [0, 401, 403, 404].includes(candidate.code ?? -1)) {
      return;
    }
    throw error;
  }
}

export { closeNestRuntime, retry, waitForShutdown };
