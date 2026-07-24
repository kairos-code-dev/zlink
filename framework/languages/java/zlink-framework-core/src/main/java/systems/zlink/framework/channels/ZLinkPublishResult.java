package systems.zlink.framework.channels;

/**
 * Reports source-local outbound transport queue admission for remote targets
 * and origin-local Spot queue admission for local targets. It does not report
 * remote Spot queue admission, an acknowledgement, or handler execution.
 */
public record ZLinkPublishResult(
    ZLinkSubmitStatus status,
    ZLinkLogicalMulticastDetail detail) {
}
