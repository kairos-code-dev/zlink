import { ZLinkSocketEventKind } from '@zlink-systems/framework';
import { NestFactory } from '@nestjs/core';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';

export async function verifyDuplicateSocketSource(): Promise<string> {
  class ValidationRequestHandler {}
  const duplicate = await assertStartupRejects(
    ZLinkModule.forRoot(zlinkFramework()
      .addRouteMesh('duplicate')
      .listen('tcp://127.0.0.1:1')
      .channel('duplicate')
      .server()
      .addRequestHandler('ValidationReq', ValidationRequestHandler)
      .options({
      monitoring: {
        socket: [
          { sourceName: 'duplicate.server' },
          { sourceName: 'duplicate.server' }
        ]
      }
      })
      .build())
  );
  return `mon-b2|duplicate=${duplicate.message}`;
}

export function verifyPollingInterval(): string {
  const interval = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework().options({
      monitoring: {
        locationRuntime: [{ sourceName: 'location', intervalMs: 0 }]
      }
    }).build());
  });
  return `mon-b2|interval=${interval.message}`;
}

export function verifyMissingLocationStore(): string {
  const missing = assertThrows(() => {
    ZLinkModule.forRoot(zlinkFramework().options({
      monitoring: {
        locationRuntime: [{ sourceName: 'missing.location', intervalMs: 100 }]
      }
    }).build());
  });
  if (!missing.message.includes('location stores')) {
    throw new Error('MON-B2 missing Location Store startup error was not explicit.');
  }
  return 'mon-b2|missing-location-store=required';
}

export async function verifyMissingSocketSource(): Promise<string> {
  class ValidationRequestHandler {}
  const missing = await assertStartupRejects(
    ZLinkModule.forRoot(zlinkFramework()
      .addRouteMesh('validation.profile')
      .listen('tcp://127.0.0.1:1')
      .channel('validation.profile')
      .server()
      .addRequestHandler('ValidationReq', ValidationRequestHandler)
      .options({
      monitoring: {
        socket: [{ sourceName: 'missing.server', events: [ZLinkSocketEventKind.ConnectionReady] }]
      }
      })
      .build())
  );
  if (!missing.message.includes('not registered')) {
    throw new Error('MON-B2 missing socket source startup error was not explicit.');
  }
  return 'mon-b2|missing-socket=not registered';
}

async function assertStartupRejects(module: ReturnType<typeof ZLinkModule.forRoot>): Promise<Error> {
  try {
    const app = await NestFactory.createApplicationContext(module, { logger: false, abortOnError: false });
    await app.close();
  } catch (error) {
    return error instanceof Error ? error : new Error(String(error));
  }
  throw new Error('Expected startup validation failure.');
}

function assertThrows(action: () => void): Error {
  try {
    action();
  } catch (error) {
    return error instanceof Error ? error : new Error(String(error));
  }
  throw new Error('Expected validation failure.');
}
