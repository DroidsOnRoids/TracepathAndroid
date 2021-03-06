#include <jni.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include "tracepath.h"

jstring JNICALL Java_pl_droidsonroids_tracepath_android_Tracepath_tracepath(JNIEnv *env, __unused jclass type, jstring jDestination, jint port) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        throwErrnoException(env, "pipe", errno);
        return NULL;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        int return_code = perform_tracepath(env, jDestination, port, pipefd[1]);
        close(pipefd[1]);
        _exit(return_code);
    } else if (pid > 0) {
        close(pipefd[1]);

        char output[MAX_OUTPUT_LENGTH + 1];
        ssize_t output_length = read(pipefd[0], output, sizeof(char) * MAX_OUTPUT_LENGTH);
        int read_errno = errno;
        output[output_length ?: 0] = '\0';

        close(pipefd[0]);

        int wstatus;
        pid_t child_pid = waitpid(pid, &wstatus, 0);
        if (child_pid < 0) {
            throwErrnoException(env, "waitpid", errno);
            return NULL;
        }

        if (WIFSIGNALED(wstatus)) {
            char message[NL_TEXTMAX];
            snprintf(message, NL_TEXTMAX, "tracepath terminated with signal %d", WTERMSIG(wstatus));
            throwIoException(env, message);
            return NULL;
        } else if (WEXITSTATUS(wstatus) != EXIT_SUCCESS) {
            throwIoException(env, output);
            return NULL;
        }

        if (output_length <= 0) {
            throwErrnoException(env, "read", read_errno);
            return NULL;
        }

        return (*env)->NewStringUTF(env, output);
    } else {
        throwErrnoException(env, "fork", errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
}

int perform_tracepath(JNIEnv *env, jstring jDestination, jint port, int fd) {
    char destination[HOST_NAME_MAX + 1];

    jsize destination_length = (*env)->GetStringUTFLength(env, jDestination);
    if (destination_length > HOST_NAME_MAX) {
        destination_length = HOST_NAME_MAX;
    }

    destination[destination_length] = '\0';
    (*env)->GetStringUTFRegion(env, jDestination, 0, destination_length, destination);

    FILE *output = fdopen(fd, "w");
    if (output == NULL) {
        return EXIT_FAILURE;
    }

    int return_code = tracepath_main(destination, (uint16_t) port, output);
    fclose(output);
    return return_code;
}
