type ShutdownOptions = {
  keepAlive?: boolean;
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

export { closeNestRuntime, waitForShutdown };
