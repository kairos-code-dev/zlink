export * from './Contracts';
export {
  DefaultZlinkStreamConnector,
} from './Runtime/ZlinkStreamConnector';
import { DefaultZlinkStreamConnector } from './Runtime/ZlinkStreamConnector';
import type { ZlinkStreamConnectorOptions } from './Contracts';

export const zlinkStreamConnectorFactory = {
  create(options: ZlinkStreamConnectorOptions): DefaultZlinkStreamConnector {
    return new DefaultZlinkStreamConnector(options);
  }
};
