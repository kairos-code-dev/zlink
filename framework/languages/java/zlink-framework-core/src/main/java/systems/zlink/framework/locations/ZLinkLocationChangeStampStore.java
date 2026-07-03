package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkLocationChangeStampStore {
    CompletionStage<Long> getChangeStampAsync(ZLinkLocationChangeStampScope scope);
}
