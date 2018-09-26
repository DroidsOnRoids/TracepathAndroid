#include <jni.h>
#include <stdio.h>

#define MAX_OUTPUT_LENGTH 32767

void throwIoException(JNIEnv *env, const char *message);
void throwErrnoException(JNIEnv *env, char *functionName, int errnoNumber);

int tracepath_main(char *destination, uint16_t port, FILE *output);

__noreturn void perform_tracepath(JNIEnv *env, jstring jDestination, jint port, int fd);
