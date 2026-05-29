// SPDX-License-Identifier: MPL-2.0

import type { BaseSocket, Context } from '../contracts';
import type { RuntimeContext } from './core/context';
import type { RuntimeBaseSocket } from './sockets';

export function asRuntimeContext(ctx: Context): RuntimeContext {
  return ctx as unknown as RuntimeContext;
}

export function asRuntimeSocket(socket: BaseSocket): RuntimeBaseSocket {
  return socket as unknown as RuntimeBaseSocket;
}

export function asPublicContract<T>(value: unknown): T {
  return value as T;
}
