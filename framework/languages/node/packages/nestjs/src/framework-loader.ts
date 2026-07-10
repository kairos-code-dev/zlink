import { createRequire } from 'node:module';
import path from 'node:path';
import type * as FrameworkIntegrationModule from '@zlink-systems/framework/nest-integration';

export type FrameworkRuntimeHost = FrameworkIntegrationModule.ZLinkNestIntegrationRuntimeHost;

function loadFramework(): typeof FrameworkIntegrationModule {
  const requireFramework = createRequire(__filename);
  const frameworkEntry = requireFramework.resolve('@zlink-systems/framework');
  return requireFramework(
    path.join(path.dirname(frameworkEntry), 'nest-integration')
  ) as typeof FrameworkIntegrationModule;
}

// The Nest package is a workspace adapter. It loads the framework's private
// composition bridge without turning that bridge into a package subpath.
export const framework = loadFramework();
