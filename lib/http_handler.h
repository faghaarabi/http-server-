#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#if defined(__has_include)
#  if __has_include(<ndbm.h>)
#    include <ndbm.h>
#  elif __has_include(<gdbm-ndbm.h>)
#    include <gdbm-ndbm.h>
#  elif __has_include(<db_185.h>)
#    include <db_185.h>
#  else
#    error "No ndbm-compatible header found. Install gdbm/ndbm headers or adjust the include."
#  endif
#else
#  include <ndbm.h>
#endif

void handle_request(int client_fd, const char *request);

#endif
