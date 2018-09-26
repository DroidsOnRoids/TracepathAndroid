Android Tracepath
=============

A [tracepath(8)](http://man7.org/linux/man-pages/man8/tracepath.8.html) wrapper for Android.
Currently only `destination` parameter is supported.

### Example

Code:
```kotlin
val output = Tracepath.tracepath("github.com")
```

Sample output:
```
1?: [LOCALHOST]                      pmtu 1500
1:  foo.bar.baz1.com                                      3.287ms
1:  foo.bar.baz2.com                                      2.736ms
2:  foo.bar.baz3com                                       3.429ms
3:  foo.bar.baz4com                                       3.539ms
4:  foo.bar.baz5.com                                      8.974ms
5:  foo.bar.baz6.com                                     17.208ms
6:  foo.bar.baz7.com                                     16.787ms
7:  foo.bar.baz8.com                                     17.686ms asymm  8
8:  foo.bar.baz9.com                                    114.357ms asymm 13
9:  no reply
10:  no reply
11:  no reply
12:  no reply
13:  no reply
14:  no reply
15:  no reply
16:  no reply
17:  no reply
18:  no reply
19:  no reply
20:  no reply
21:  no reply
22:  no reply
23:  no reply
24:  no reply
25:  no reply
26:  no reply
27:  no reply
28:  no reply
29:  no reply
30:  no reply
Too many hops: pmtu 1500
Resume: pmtu 1500
```

### Download
```gradle
repositories {
    maven { url 'https://oss.sonatype.org/content/repositories/snapshots' }
}

dependencies {
    implementation 'pl.droidsonroids:tracepath-android:0.0.2-SNAPSHOT'
}
```

### License
Library uses the GPL v2 License. See [LICENSE](LICENSE) file.