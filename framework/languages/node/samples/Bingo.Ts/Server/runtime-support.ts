function waitForShutdown(options: any = {}) {
  return new Promise((resolve) => {
    const keepAlive = options.keepAlive === true
      ? setInterval(() => {}, 60000)
      : undefined;
    const stop = () => {
      if (keepAlive !== undefined) {
        clearInterval(keepAlive);
      }
      resolve(undefined);
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

export { closeNestRuntime, waitForShutdown };
