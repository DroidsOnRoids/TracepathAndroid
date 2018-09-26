package pl.droidsonroids.tracepath.android

import org.assertj.core.api.Java6Assertions.assertThat
import org.assertj.core.api.Java6Assertions.assertThatThrownBy
import org.junit.Test
import java.io.IOException

class TracepathTest {

    @Test
    fun testLocalhostTracepath() {
        val output = Tracepath.tracepath("localhost")

        assertThat(output).hasLineCount(2)
        assertThat(output.lines()[1].trim()).startsWith("Resume: pmtu 65535 hops 1 back 1")
        assertThat(output.lines()[0]).contains("localhost")
    }

    @Test
    fun testInvalidHostTracepath() {
        assertThatThrownBy { Tracepath.tracepath("test.invalid") }
            .isInstanceOf(IOException::class.java)
            .hasMessageContaining("test.invalid: No address associated with hostname")
    }

}