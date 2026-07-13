type ShutdownOptions = {
  keepAlive?: boolean;
};

const bingoMetricValues = new Map<string, number>();

const bingoMeterProvider = {
  getMeter(name: string) {
    void name;
    const instrument = (metricName: string) => ({
      add(value: number): void {
        bingoMetricValues.set(metricName, (bingoMetricValues.get(metricName) ?? 0) + value);
      },
      record(value: number): void {
        bingoMetricValues.set(metricName, value);
      }
    });
    return {
      createCounter: instrument,
      createUpDownCounter: instrument,
      createHistogram: instrument
    };
  }
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

export { bingoMeterProvider, bingoMetricValues, closeNestRuntime, waitForShutdown };
