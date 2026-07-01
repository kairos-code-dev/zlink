package systems.zlink.e2e.kotlin.runtimemonitoring.client

import com.fasterxml.jackson.databind.ObjectMapper

fun main(args: Array<String>) {
    ClientScenario(ObjectMapper()).run()
    println("runtime-monitoring kotlin e2e result=passed")
}
