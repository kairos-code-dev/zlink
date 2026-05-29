// SPDX-License-Identifier: MPL-2.0

import type { Message } from '../messaging';
import type { BaseSocket } from '../sockets';

export type ZlinkVersion = readonly [number, number, number];

export interface ZlinkRuntimeFacade {
  version(): ZlinkVersion;
  strerror(code: number): string;
  has(capability: string): boolean;
  proxy(frontend: BaseSocket, backend: BaseSocket, capture?: BaseSocket): void;
  proxySteerable(
    frontend: BaseSocket,
    backend: BaseSocket,
    capture: BaseSocket | null,
    control: BaseSocket
  ): void;
  sleep(seconds: number): void;
  multipartClose(parts: Message[]): void;
}
