#include "tracepath.h"

void throwErrnoException(JNIEnv *env, char *functionName, int errnoNumber) {
    jclass const exceptionClass = (*env)->FindClass(env, "android/system/ErrnoException");
    if (exceptionClass == NULL) {
        return;
    }
    jmethodID constructorID = (*env)->GetMethodID(env, exceptionClass, "<init>", "(Ljava/lang/String;I)V");
    if (constructorID == NULL) {
        return;
    }
    jstring functionNameUTF = (*env)->NewStringUTF(env, functionName);
    jobject exception = (*env)->NewObject(env, exceptionClass, constructorID, functionNameUTF, errnoNumber);
    (*env)->Throw(env, exception);
}

void throwIoException(JNIEnv *env, const char *message) {
    jclass const exceptionClass = (*env)->FindClass(env, "java/io/IOException");
    if (exceptionClass == NULL) {
        return;
    }
    (*env)->ThrowNew(env, exceptionClass, message);
}