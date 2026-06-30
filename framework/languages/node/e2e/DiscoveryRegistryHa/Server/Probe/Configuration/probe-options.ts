import { parseArgs, required } from '../../Registry/Configuration/registry-options';

export interface ProbeOptions {
  readonly httpUrl: string;
  readonly registryRouterEndpoint: string;
  readonly logDir: string;
}

export function parseProbeOptions(args: readonly string[]): ProbeOptions {
  const values = parseArgs(args);
  return {
    httpUrl: required(values, 'http-url'),
    registryRouterEndpoint: required(values, 'registry-router-endpoint'),
    logDir: values.get('log-dir') ?? 'logs'
  };
}
