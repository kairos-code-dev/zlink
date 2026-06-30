import { parseArgs, required } from '../../Registry/Configuration/registry-options';

export interface ConsumerOptions {
  readonly httpUrl: string;
  readonly logDir: string;
  readonly registryRouterEndpoints: readonly string[];
  readonly traceLabel: string;
}

export function parseConsumerOptions(args: readonly string[]): ConsumerOptions {
  const values = parseArgs(args);
  const registryRouterEndpoints = values.getAll('registry-router-endpoint').length === 0
    ? [required(values, 'registry-router-endpoint')]
    : values.getAll('registry-router-endpoint');
  return {
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    registryRouterEndpoints,
    traceLabel: values.get('trace-label') ?? 'consumer'
  };
}
