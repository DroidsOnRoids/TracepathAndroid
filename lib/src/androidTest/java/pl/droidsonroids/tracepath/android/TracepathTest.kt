package pl.droidsonroids.tracepath.android

import org.assertj.core.api.Java6Assertions.assertThat
import org.assertj.core.api.Java6Assertions.assertThatThrownBy
import org.junit.Test
import java.io.IOException

class TracepathTest {

    @Test
    fun testLocalhostTracepath() {
        val output = Tracepath.tracepath("localhost")

        val lines = output.lines()
        assertThat(lines.size).isGreaterThanOrEqualTo(2)
        assertThat(lines.last(String::isNotBlank).trim()).startsWith("Resume: pmtu")
        assertThat(lines.first()).contains("localhost")
    }

    @Test
    fun testLocalhostTracepathWithPort() {
        val output = Tracepath.tracepath("localhost", 1234)

        val lines = output.lines()
        assertThat(lines.size).isGreaterThanOrEqualTo(2)
        assertThat(lines.last(String::isNotBlank).trim()).startsWith("Resume: pmtu")
        assertThat(lines.first()).contains("localhost")
    }

    @Test
    fun testInvalidHostTracepath() {
        assertThatThrownBy { Tracepath.tracepath("test.invalid") }
            .isInstanceOf(IOException::class.java)
            .hasMessageContaining("test.invalid: No address associated with hostname")
    }

}