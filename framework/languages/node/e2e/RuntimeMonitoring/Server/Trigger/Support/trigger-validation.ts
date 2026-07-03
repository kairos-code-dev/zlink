import { createFrameworkRegistration, ZLinkSocketEventKind } from '@zlink-systems/framework';

export function verifyDuplicateSocketSource(): string {
  const duplicate = assertThrows(() => {
    createFrameworkRegistration({
      channels: {
        duplicate: { server: { bind: 'tcp://127.0.0.1:1' } }
      },
      monitoring: {
        socket: [
          { sourceName: 'duplicate.server' },
          { sourceName: 'duplicate.server' }
        ]
      },
      routeChannels: [],
      spotFactories: [],
      spotNodes: {},
      streamNodes: {}
    });
  });
  return `mon-b2|duplicate=${duplicate.message}`;
}

export function verifyPollingInterval(): string {
  const interval = assertThrows(() => {
    createFrameworkRegistration({
      channels: {},
      monitoring: {
        spot: [{ sourceName: 'spot', intervalMs: 0 }]
      },
      routeChannels: [],
      spotFactories: [],
      spotNodes: {},
      streamNodes: {}
    });
  });
  return `mon-b2|interval=${interval.message}`;
}

export function verifyMissingSpotSource(): string {
  const missing = assertThrows(() => {
    createFrameworkRegistration({
      channels: {},
      monitoring: {
        spot: [{ sourceName: 'missing.spot', intervalMs: 100 }]
      },
      routeChannels: [],
      spotFactories: [],
      spotNodes: {},
      streamNodes: {}
    });
  });
  if (!missing.message.includes('not registered')) {
    throw new Error('MON-B2 missing spot source startup error was not explicit.');
  }
  return 'mon-b2|missing-spot=not registered';
}

export function verifyMissingSocketSource(): string {
  const missing = assertThrows(() => {
    createFrameworkRegistration({
      channels: {
        'validation.profile': { server: { bind: 'tcp://127.0.0.1:1' } }
      },
      monitoring: {
        socket: [{ sourceName: 'missing.server', events: [ZLinkSocketEventKind.ConnectionReady] }]
      },
      routeChannels: [],
      spotFactories: [],
      spotNodes: {},
      streamNodes: {}
    });
  });
  if (!missing.message.includes('not registered')) {
    throw new Error('MON-B2 missing socket source startup error was not explicit.');
  }
  return 'mon-b2|missing-socket=not registered';
}

function assertThrows(action: () => void): Error {
  try {
    action();
  } catch (error) {
    return error instanceof Error ? error : new Error(String(error));
  }
  throw new Error('Expected validation failure.');
}
