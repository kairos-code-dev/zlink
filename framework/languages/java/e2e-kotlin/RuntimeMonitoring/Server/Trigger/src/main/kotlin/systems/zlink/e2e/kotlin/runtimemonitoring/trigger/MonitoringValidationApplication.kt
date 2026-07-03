package systems.zlink.e2e.kotlin.runtimemonitoring.trigger

import org.springframework.boot.SpringBootConfiguration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.EnableAutoConfiguration
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer
import java.time.Duration

object MonitoringValidationApplication {
    @JvmStatic
    fun validateRegistrationFailures() {
        verifyDuplicateSocketSource()
        verifyPollingInterval()
        verifyMissingSocketSource()
        verifyMissingSpotSource()
    }

    @JvmStatic
    fun verifyDuplicateSocketSource(): String {
        return "mon-b2|duplicate=" +
            expectFailure(DuplicateSocketSourceConfig::class.java, "Duplicate monitoring socket source")
    }

    @JvmStatic
    fun verifyPollingInterval(): String {
        return "mon-b2|interval=" +
            expectFailure(BadIntervalConfig::class.java, "location runtime interval must be positive")
    }

    @JvmStatic
    fun verifyMissingSocketSource(): String {
        return "mon-b2|missing-socket=" +
            expectFailure(
                MissingSocketSourceConfig::class.java,
                "monitoring socket source is not configured",
            )
    }

    @JvmStatic
    fun verifyMissingSpotSource(): String {
        return "mon-b2|missing-spot=" +
            expectFailure(
                MissingSpotSourceConfig::class.java,
                "monitoring spot source is not configured",
            )
    }

    @JvmStatic
    fun expectFailure(
        configClass: Class<*>,
        messagePart: String,
    ): String {
        try {
            SpringApplicationBuilder(configClass)
                .web(WebApplicationType.NONE)
                .run()
                .use {
                    throw IllegalStateException("${configClass.simpleName} unexpectedly started")
                }
        } catch (error: Throwable) {
            val message = matchingMessage(error, messagePart)
            if (message == null) {
                throw IllegalStateException(
                    "${configClass.simpleName} failed with unexpected error",
                    error,
                )
            }
            return message
        }
    }

    private fun matchingMessage(
        error: Throwable,
        messagePart: String,
    ): String? {
        var current: Throwable? = error
        while (current != null) {
            val message = current.message
            if (message != null && message.contains(messagePart)) {
                return message
            }
            current = current.cause
        }
        return null
    }

    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    class DuplicateSocketSourceConfig {
        @Bean
        fun duplicateSocketMonitoring(): ZLinkMonitoringOptionsCustomizer {
            return ZLinkMonitoringOptionsCustomizer { options ->
                options.addSocketEvents("duplicate.server")
                options.addSocketEvents("duplicate.server")
            }
        }
    }

    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    class BadIntervalConfig {
        @Bean
        fun badIntervalMonitoring(): ZLinkMonitoringOptionsCustomizer {
            return ZLinkMonitoringOptionsCustomizer { options ->
                options.addLocationRuntimeEvents(Contracts.LOCATION_SOURCE, Duration.ZERO)
            }
        }
    }

    @EnableZLinkFramework
    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    class MissingSocketSourceConfig {
        @Bean
        fun missingSocketMonitoring(): ZLinkMonitoringOptionsCustomizer {
            return ZLinkMonitoringOptionsCustomizer { options ->
                options.addSocketEvents("missing.socket")
            }
        }
    }

    @EnableZLinkFramework
    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    class MissingSpotSourceConfig {
        @Bean
        fun missingSpotMonitoring(): ZLinkMonitoringOptionsCustomizer {
            return ZLinkMonitoringOptionsCustomizer { options ->
                options.addSpotEvents("missing.spot", Duration.ofMillis(100))
            }
        }
    }
}
