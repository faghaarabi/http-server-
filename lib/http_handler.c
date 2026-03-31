#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include "http_handler.h"

#define BUFFER_SIZE 4096
#define WWW_ROOT "./www"
#define MAX_PATH_SIZE 512
#define DB_PATH "db/data.db"

static ssize_t safe_write(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *ptr = (const char *)buf;

    while (total < len) {
        ssize_t sent = write(fd, ptr + total, len - total);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)sent;
    }
    return (ssize_t)total;
}

/* “application/octet-stream is a generic binary type used when the server
 doesn’t recognize the file format, so the browser treats it as a downloadable file.”*/


static const char *get_content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=UTF-8";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=UTF-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".txt") == 0) return "text/plain; charset=UTF-8";

    return "application/octet-stream";
}

static int send_response(int client_fd, int code, const char *reason,
                         const char *content_type, const void *body, size_t body_len,
                         int is_head) {
    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.0 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     code, reason, content_type, body_len);

    if (n < 0 || (size_t)n >= sizeof(header)) {
        return -1;
    }
    if (safe_write(client_fd, header, (size_t)n) < 0) {
        return -1;
    }
    if (!is_head && body && body_len > 0) {
        if (safe_write(client_fd, body, body_len) < 0) {
            return -1;
        }
    }
    return 0;
}

static void send_error(int client_fd, int code, const char *reason, const char *message, int is_head) {
    (void)send_response(client_fd, code, reason, "text/plain; charset=UTF-8",
                        message, strlen(message), is_head);
}

static int is_safe_path(const char *path) {
    return strstr(path, "..") == NULL;
}

static void send_file(int client_fd, const char *path, int is_head) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            send_error(client_fd, 404, "Not Found", "404 Not Found\n", is_head);
        } else {
            send_error(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n", is_head);
        }
        return;
    }

    {
        struct stat st;
        if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
            close(fd);
            send_error(client_fd, 404, "Not Found", "404 Not Found\n", is_head);
            return;
        }

        if (send_response(client_fd, 200, "OK", get_content_type(path), NULL, (size_t)st.st_size, 1) < 0) {
            close(fd);
            return;
        }
    }

    if (is_head) {
        close(fd);
        return;
    }

    {
        char buffer[BUFFER_SIZE];
        ssize_t bytes;
        while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
            if (safe_write(client_fd, buffer, (size_t)bytes) < 0) {
                break;
            }
        }
    }

    close(fd);
}

/// marks the end of headers and the start of the body.
static const char *find_body(const char *request) {
    const char *body = strstr(request, "\r\n\r\n");
    return body ? body + 4 : NULL;
}

static int store_post_data(const char *data) {
    FILE *tmp;
    char key_buf[128];
    int rc;

    DBM *db = dbm_open(DB_PATH, O_RDWR | O_CREAT, 0644);
    if (!db) {
        perror("dbm_open failed");
        return -1;
    }
// generates a unique key based on timestamp + pid
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        snprintf(key_buf, sizeof(key_buf), "%ld-%ld-%ld",
                 (long)ts.tv_sec, (long)ts.tv_nsec, (long)getpid());
    }

    {
        datum key;
        datum value;
        key.dptr = key_buf;
        key.dsize = (int)strlen(key_buf);
        value.dptr = (char *)data;
        value.dsize = (int)strlen(data);
        rc = dbm_store(db, key, value, DBM_REPLACE);
    }

    dbm_close(db);

    if (rc != 0) {
        perror("dbm_store failed");
        return -1;
    }

    tmp = fopen("db/.db_write_test", "a");
    if (tmp) {
        fclose(tmp);
        unlink("db/.db_write_test");
    }
    return 0;
}

void handle_request(int client_fd, const char *request) {
    char method[16];
    char path[256];
    char version[32];
    int is_head;

    if (sscanf(request, "%15s %255s %31s", method, path, version) != 3) {
        send_error(client_fd, 400, "Bad Request", "400 Bad Request\n", 0);
        return;
    }

    is_head = (strcmp(method, "HEAD") == 0);

    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
        send_error(client_fd, 400, "Bad Request", "400 Bad Request\n", is_head);
        return;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0) {
        send_error(client_fd, 405, "Method Not Allowed here", "405 Method Not Allowed\n", is_head);
        return;
    }

    if (!is_safe_path(path)) {
        send_error(client_fd, 403, "Forbidden", "403 Forbidden\n", is_head);
        return;
    }

    if (strcmp(method, "POST") == 0) {
        const char *body = find_body(request);
        const char *msg = "POST data stored\n";

        if (!body) {
            send_error(client_fd, 400, "Bad Request", "400 Bad Request\n", 0);
            return;
        }
        if (store_post_data(body) != 0) {
            send_error(client_fd, 500, "Internal Server Error", "500 Internal Server Error\n", 0);
            return;
        }

        (void)send_response(client_fd, 200, "OK", "text/plain; charset=UTF-8", msg, strlen(msg), 0);
        return;
    }

    if (strcmp(path, "/") == 0) {
        strncpy(path, "/index.html", sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    {
        char full_path[MAX_PATH_SIZE];
        int n = snprintf(full_path, sizeof(full_path), "%s%s", WWW_ROOT, path);
        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            send_error(client_fd, 414, "URI Too Long", "414 URI Too Long\n", is_head);
            return;
        }
        send_file(client_fd, full_path, is_head);
    }
}
