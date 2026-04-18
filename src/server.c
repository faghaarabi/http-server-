#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 9090
#define DEFAULT_BACKLOG 32
#define DEFAULT_WORKERS 4
#define MAX_WORKERS 128
#define INITIAL_REQ_CAP 8192
#define MAX_REQ_SIZE (1024 * 1024)
#define DEFAULT_LIB_PATH "lib/libhttp.so"

static volatile sig_atomic_t shutting_down = 0;
static volatile sig_atomic_t sigchld_seen = 0;
static int listen_fd = -1;
static pid_t worker_pids[MAX_WORKERS];
static int worker_count = DEFAULT_WORKERS;
static int server_port = DEFAULT_PORT;
static int server_backlog = DEFAULT_BACKLOG;
static const char *library_path = DEFAULT_LIB_PATH;

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-p port] [-w workers] [-b backlog] [-l shared_library]\n",
            prog);
}

static void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void handle_sigterm_or_sigint(int sig) {
    (void)sig;
    shutting_down = 1;
}

static void handle_sigchld(int sig) {
    (void)sig;
    sigchld_seen = 1;
}

static ssize_t safe_write(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *)buf;

    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static int create_listen_socket(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static char *find_header_end(char *buf, size_t len) {
    size_t i;

    if (len < 4) {
        return NULL;
    }

    for (i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return buf + i;
        }
    }
    return NULL;
}

static int parse_content_length(const char *headers) {
    const char *p = headers;

    while (*p != '\0') {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) {
            break;
        }
        if (line_end == p) {
            break;
        }

        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            const char *value = p + 15;
            char *endptr = NULL;
            long parsed;

            while (*value == ' ' || *value == '\t') {
                ++value;
            }

            parsed = strtol(value, &endptr, 10);
            if (endptr == value || parsed < 0 || parsed > (long)MAX_REQ_SIZE) {
                return -2;
            }
            return (int)parsed;
        }

        p = line_end + 2;
    }

    return -1;
}

static int send_simple_response(int client_fd, int code, const char *reason, const char *body) {
    char header[512];
    size_t body_len = body ? strlen(body) : 0U;
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.0 %d %s\r\n"
                     "Content-Type: text/plain; charset=UTF-8\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     code, reason, body_len);

    if (n < 0 || (size_t)n >= sizeof(header)) {
        return -1;
    }
    if (safe_write(client_fd, header, (size_t)n) < 0) {
        return -1;
    }
    if (body_len > 0 && safe_write(client_fd, body, body_len) < 0) {
        return -1;
    }
    return 0;
}

static char *read_http_request(int client_fd) {
    size_t cap = INITIAL_REQ_CAP;
    size_t used = 0;
    char *buf = malloc(cap + 1);
    int content_length = 0;
    int saw_headers = 0;
    size_t total_needed = 0;

    if (!buf) {
        return NULL;
    }

    for (;;) {
        ssize_t n;

        if (used == cap) {
            size_t new_cap;
            char *tmp;

            if (cap >= MAX_REQ_SIZE) {
                free(buf);
                return NULL;
            }

            new_cap = cap * 2;
            if (new_cap > MAX_REQ_SIZE) {
                new_cap = MAX_REQ_SIZE;
            }

            tmp = realloc(buf, new_cap + 1);
            if (!tmp) {
                free(buf);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }

        n = read(client_fd, buf + used, cap - used);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return NULL;
        }
        if (n == 0) {
            break;
        }

        used += (size_t)n;
        buf[used] = '\0';

        if (!saw_headers) {
            char *hdr_end = find_header_end(buf, used);
            if (hdr_end) {
                size_t header_bytes = (size_t)(hdr_end - buf) + 4;

                saw_headers = 1;
                content_length = parse_content_length(buf);
                if (content_length == -2) {
                    free(buf);
                    return NULL;
                }
                if (content_length < 0) {
                    content_length = 0;
                }

                total_needed = header_bytes + (size_t)content_length;
                if (total_needed > MAX_REQ_SIZE) {
                    free(buf);
                    return NULL;
                }
                if (used >= total_needed) {
                    break;
                }
            }
        } else if (used >= total_needed) {
            break;
        }
    }

    if (!saw_headers) {
        free(buf);
        return NULL;
    }

    buf[used] = '\0';
    return buf;
}

static void worker_loop(int shared_listen_fd) {
    void *handle = NULL;
    void (*handle_request)(int, const char *) = NULL;
    time_t last_modified = 0;

    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        int client_fd = accept(shared_listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (shutting_down) {
                    break;
                }
                continue;
            }
            perror("accept failed");
            continue;
        }

        {
            struct stat st;

            if (stat(library_path, &st) != 0) {
                perror("stat shared library");
                send_simple_response(client_fd, 500, "Internal Server Error",
                                     "Shared library unavailable\n");
                close(client_fd);
                continue;
            }

            if (!handle || st.st_mtime != last_modified) {
                void *new_handle = dlopen(library_path, RTLD_NOW);
                void (*new_handle_request)(int, const char *);
                const char *err;

                if (!new_handle) {
                    fprintf(stderr, "dlopen error: %s\n", dlerror());
                    send_simple_response(client_fd, 500, "Internal Server Error",
                                         "Could not load request handler\n");
                    close(client_fd);
                    continue;
                }

                dlerror();
                new_handle_request = (void (*)(int, const char *))dlsym(new_handle, "handle_request");
                err = dlerror();
                if (err != NULL || !new_handle_request) {
                    fprintf(stderr, "dlsym error: %s\n", err ? err : "unknown error");
                    dlclose(new_handle);
                    send_simple_response(client_fd, 500, "Internal Server Error",
                                         "Could not resolve request handler\n");
                    close(client_fd);
                    continue;
                }

                if (handle) {
                    dlclose(handle);
                }
                handle = new_handle;
                handle_request = new_handle_request;
                last_modified = st.st_mtime;
                fprintf(stderr, "[%ld] Reloaded [UPDATED AFTER MAKING ME ANGRY] shared library %s\n", (long)getpid(), library_path);
            }
        }

        {
            char *request = read_http_request(client_fd);
            if (!request) {
                send_simple_response(client_fd, 400, "Bad Request",
                                     "Malformed or incomplete HTTP request\n");
                close(client_fd);
                continue;
            }

            handle_request(client_fd, request);
            free(request);
        }

        shutdown(client_fd, SHUT_WR);
        {
            char drain[1024];
            while (read(client_fd, drain, sizeof(drain)) > 0) {
            }
        }
        close(client_fd);
    }

    if (handle) {
        dlclose(handle);
    }
}

static int find_worker_slot(pid_t pid) {
    int i;
    for (i = 0; i < worker_count; ++i) {
        if (worker_pids[i] == pid) {
            return i;
        }
    }
    return -1;
}

static pid_t spawn_worker_at_slot(int slot) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        worker_loop(listen_fd);
        _exit(EXIT_SUCCESS);
    }
    worker_pids[slot] = pid;
    return pid;
}

// Starts all worker processes at server startup and tracks their PIDs.
static void spawn_initial_workers(void) {
    int i;
    for (i = 0; i < worker_count; ++i) {
        worker_pids[i] = 0;
        if (spawn_worker_at_slot(i) < 0) {
            error_exit("fork failed");
        }
    }
}

// Reaps dead worker processes and respawns new ones to keep the worker pool alive.
static void respawn_dead_workers(void) {
    int status = 0;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int slot = find_worker_slot(pid);
        if (slot >= 0) {
            worker_pids[slot] = 0;
            if (!shutting_down) {
                pid_t new_pid = spawn_worker_at_slot(slot);
                if (new_pid < 0) {
                    perror("respawn worker failed");
                } else {
                    fprintf(stderr,
                            "Worker %ld exited -> Respawned child PID: %ld\n",
                            (long)pid,
                            (long)new_pid);
                }
            }
        }
    }
    sigchld_seen = 0;
}

// Sends SIGTERM to all workers, waits for them to exit, and cleans up their PIDs.
static void terminate_workers(void) {
    int i;
    for (i = 0; i < worker_count; ++i) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }
    for (i = 0; i < worker_count; ++i) {
        if (worker_pids[i] > 0) {
            waitpid(worker_pids[i], NULL, 0);
            worker_pids[i] = 0;
        }
    }
}

//./server_app -p <port> -w <workers> -b <backlog> -l <library_path>
static void parse_args(int argc, char **argv) {
    int opt;

    while ((opt = getopt(argc, argv, "p:w:b:l:")) != -1) {
        switch (opt) {
            case 'p':
                server_port = atoi(optarg);
                if (server_port <= 0 || server_port > 65535) {
                    fprintf(stderr, "Invalid port: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'w':
                worker_count = atoi(optarg);
                if (worker_count <= 0 || worker_count > MAX_WORKERS) {
                    fprintf(stderr, "Worker count must be between 1 and %d\n", MAX_WORKERS);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'b':
                server_backlog = atoi(optarg);
                if (server_backlog <= 0) {
                    fprintf(stderr, "Invalid backlog: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'l':
                library_path = optarg;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv) {
    struct sigaction sa_term;
    struct sigaction sa_chld;

    parse_args(argc, argv);

    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = handle_sigterm_or_sigint;
    sigemptyset(&sa_term.sa_mask);
    if (sigaction(SIGINT, &sa_term, NULL) < 0) {
        error_exit("sigaction SIGINT");
    }
    if (sigaction(SIGTERM, &sa_term, NULL) < 0) {
        error_exit("sigaction SIGTERM");
    }

    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) {
        error_exit("sigaction SIGCHLD");
    }

    listen_fd = create_listen_socket(server_port, server_backlog);
    if (listen_fd < 0) {
        error_exit("create listen socket");
    }

    fprintf(stderr, "Server listening on port %d with %d workers\n", server_port, worker_count);
    spawn_initial_workers();

    while (!shutting_down) {
        pause();
        if (sigchld_seen) {
            respawn_dead_workers();
        }
    }

    close(listen_fd);
    terminate_workers();
    return 0;
}
