import { parseArgs, required } from '../../Shared/Configuration/server-options';

export interface PublisherOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly publisherEndpoint: string;
  readonly evidenceFile?: string;
}

export function parsePublisherOptions(args: readonly string[]): PublisherOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? 'publisher',
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    publisherEndpoint: required(values, 'publisher-endpoint'),
    evidenceFile: values.get('evidence-file')
  };
}
