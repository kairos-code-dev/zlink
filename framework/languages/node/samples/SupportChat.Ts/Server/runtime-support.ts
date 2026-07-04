import http from 'node:http';

function endpointPort(endpoint: string): number {
  const parsed = new URL(endpoint.replace('tcp://', 'http://'));
  return Number(parsed.port);
}

function waitForShutdown(): Promise<void> {
  return new Promise<void>((resolve) => {
    const keepAlive = setInterval(() => undefined, 1000);
    const stop = () => {
      clearInterval(keepAlive);
      resolve();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
}

async function closeServer(server: http.Server): Promise<void> {
  await new Promise<void>((resolve) => server.close(() => resolve()));
}

export {
  endpointPort,
  waitForShutdown,
  closeServer
};
