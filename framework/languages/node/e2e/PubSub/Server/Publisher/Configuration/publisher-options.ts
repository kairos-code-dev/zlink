import { objectValues, optional, required } from '../../Shared/Configuration/server-options';

export interface PublisherOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly publisherEndpoint: string;
  readonly evidenceFile?: string;
}

export function validatePublisherOptions(value: unknown): PublisherOptions {
  const values = objectValues(value);
  return {
    rid: optional(values, 'rid') ?? 'publisher',
    httpUrl: optional(values, 'httpUrl') ?? 'http://127.0.0.1:0',
    logDir: optional(values, 'logDir') ?? 'logs',
    publisherEndpoint: required(values, 'publisherEndpoint'),
    evidenceFile: optional(values, 'evidenceFile')
  };
}
