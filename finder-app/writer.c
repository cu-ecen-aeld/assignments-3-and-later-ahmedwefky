#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3) {
        fprintf(stderr, "Usage: writer <full-path-to-file> <text>\n");
        syslog(LOG_ERR, "Missing arguments");
        closelog();
        return 1;
    }

    const char *path = argv[1];
    const char *text = argv[2];

    FILE *f = fopen(path, "w");
    if (!f) {
        syslog(LOG_ERR, "Failed to open %s: %s", path, strerror(errno));
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        closelog();
        return 1;
    }

    if (fputs(text, f) == EOF) {
        syslog(LOG_ERR, "Failed writing to %s: %s", path, strerror(errno));
        fprintf(stderr, "Error writing to %s: %s\n", path, strerror(errno));
        fclose(f);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", text, path);

    fclose(f);

    closelog();
    return 0;
}