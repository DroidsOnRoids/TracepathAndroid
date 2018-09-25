#include <jni.h>
#include <stdio.h>

#define ARGC 2
#define MAX_OUTPUT_LENGTH 32767

void throwIoException(JNIEnv *env, const char *message);
void throwErrnoException(JNIEnv *env, char *functionName, int errnoNumber);
int tracepath_main(int argc, const char **argv, FILE *output);
__noreturn void perform_tracepath(JNIEnv *env, jstring jDestination, int fd);
