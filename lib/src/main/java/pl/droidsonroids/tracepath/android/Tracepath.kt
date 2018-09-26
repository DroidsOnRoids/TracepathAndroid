package pl.droidsonroids.tracepath.android

import android.system.ErrnoException
import java.io.IOException

/**
 * [Tracepath][http://man7.org/linux/man-pages/man8/tracepath.8.html] wrapper
 */
object Tracepath {
    init {
        System.loadLibrary("tracepath")
    }

    /**
     * @return tracepath output
     * @throws IOException when I/O error occurs e.g. host is unknown
     * @throws ErrnoException when native tracepath process cannot be started or output cannot be retrieved
     */
    @Throws(IOException::class, ErrnoException::class)
    @JvmStatic
    external fun tracepath(destination: String): String
}