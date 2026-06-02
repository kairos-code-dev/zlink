import { createRequire } from 'node:module';
import path from 'node:path';

type ZLinkBindingModule = {
  version(): [number, number, number];
};

const requireBinding = createRequire(__filename);

export function loadBinding(): ZLinkBindingModule {
  try {
    return requireBinding('@zlink-systems/zlink') as ZLinkBindingModule;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }

    const localBindingEntry = path.resolve(
      __dirname,
      '../../../../../../../../bindings/node/dist'
    );
    return requireBinding(localBindingEntry) as ZLinkBindingModule;
  }
}

export interface ZLinkNodeBindingInfo {
  readonly version: readonly [number, number, number];
}

export function getNodeBindingInfo(): ZLinkNodeBindingInfo {
  return { version: loadBinding().version() };
}
