import { ZLinkSocketEventKind } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';

export function verifyDuplicateSocketSource(): string {
  const duplicate = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework()
      .addClientServerChannel('duplicate')
      .enableServer('tcp://127.0.0.1:1')
      .options({
      monitoring: {
        socket: [
          { sourceName: 'duplicate.server' },
          { sourceName: 'duplicate.server' }
        ]
      }
      })
      .build());
  });
  return `mon-b2|duplicate=${duplicate.message}`;
}

export function verifyPollingInterval(): string {
  const interval = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework().options({
      monitoring: {
        spot: [{ sourceName: 'spot', intervalMs: 0 }]
      }
    }).build());
  });
  return `mon-b2|interval=${interval.message}`;
}

export function verifyMissingSpotSource(): string {
  const missing = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework().options({
      monitoring: {
        spot: [{ sourceName: 'missing.spot', intervalMs: 100 }]
      }
    }).build());
  });
  if (!missing.message.includes('not registered')) {
    throw new Error('MON-B2 missing spot source startup error was not explicit.');
  }
  return 'mon-b2|missing-spot=not registered';
}

export function verifyMissingSocketSource(): string {
  const missing = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework()
      .addClientServerChannel('validation.profile')
      .enableServer('tcp://127.0.0.1:1')
      .options({
      monitoring: {
        socket: [{ sourceName: 'missing.server', events: [ZLinkSocketEventKind.ConnectionReady] }]
      }
      })
      .build());
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
