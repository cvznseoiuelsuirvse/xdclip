#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xdwayland-client.h>
#include <xdwayland-core.h>
#include <xdwayland-version.h>

#include "../wlr-data-control-unstable-v1-protocol.h"
#include "util.h"

#define ENSURE_RESULT(n)                                                       \
  if ((n) == -1)                                                               \
  __abort(data)

#define ROUNDTRIP ENSURE_RESULT(xdwl_roundtrip(data->proxy))

enum data_types {
  TEXT,
  BINARY,
  PNG,
  JPG,
  MP4,
  MP3,
  URI,
};

struct data {
  char *buffer;
  size_t size;
  xdwl_proxy *proxy;
  enum data_types type;
  size_t mimes_size;
  const char *mimes[5];
};

void print_help() {
  printf("usage: xdcopy [-h] [-f FILE] [DATA]\n");
  printf("  -h, --help    show this message\n");
  printf("  -f, --file    copy file\n");
  printf("\n");
  printf("  noargs        copy from stdin\n");
  printf("  DATA          copy DATA\n");
}

void __abort(struct data *data) {
  xdwl_error_print();
  if (data->proxy)
    xdwl_proxy_destroy(data->proxy);
  if (data->buffer)
    free(data->buffer);
  exit(1);
}

void handle_wl_display_delete_id(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  size_t o_id = args[1].u;

  if ((xdwl_object_unregister(proxy, o_id)) == -1) {
    __print_error("xdcopy", "Failed to unregister %d\n", o_id);
    __abort(data);
  }
}

void handle_wl_display_error(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  uint32_t object_id = args[1].u;
  uint32_t code = args[2].u;
  const char *message = args[3].s;

  const xdwl_object *object = xdwl_object_get_by_id(proxy, object_id);
  const char *object_name = object->name;

  __print_error("xdcopy", "%s#%d (code %d): %s\n", object_name, object_id, code,
                message);
  __abort(data);
}

void handle_wlr_data_control_device_data_offer(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  xdwl_id new_id = args[1].u;

  if (xdwl_object_register(proxy, new_id, "zwlr_data_control_offer_v1") == 0)
    __abort(data);
}

void handle_wlr_data_control_source_send(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;

  int fd = args[2].fd;

  int written;
  if ((written = write(fd, data->buffer, data->size)) != (int)data->size) {
    perror("write");
    __print_error("xdcopy",
                  "error during handling zwlr_data_control_source_v1.send(). "
                  "failed to write %d bytes to fd %d\n",
                  data->size, fd, written);
    close(fd);
    __abort(data);
  };

  close(fd);
}

void handle_wlr_data_control_source_cancelled(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy_destroy(data->proxy);
  free(data->buffer);
  exit(0);
}

void handle_wl_registry_global(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  uint32_t name = args[1].u;
  const char *interface = args[2].s;
  uint32_t version = args[3].u;

  int object_id = 0;
  if (STREQ(interface, "zwlr_data_control_manager_v1") ||
      STREQ(interface, "wl_seat")) {

    object_id = xdwl_object_register(proxy, 0, interface);
    if (object_id == 0) {
      __abort(data);
    }

    xdwl_registry_bind(proxy, 0, name, interface, version, object_id);
    ROUNDTRIP;
  }
}

void assign_mimes(struct data *data) {
  if (!is_binary(data->buffer, data->size - 1)) {
    data->mimes[0] = "text/plain";
    data->mimes[1] = "text/plain;charset=utf-8";
    data->mimes[2] = "TEXT";
    data->mimes[3] = "STRING";
    data->mimes[4] = "UTF8_STRING";
    data->mimes_size = 5;
    return;
  }

  char *buffer = data->buffer;

  data->type = BINARY;
  data->mimes[0] = "application/octet-stream";
  data->mimes_size = 1;

  if (data->size >= 4) {
    uint32_t first2b = buffer[0] << 8 | buffer[1];
    uint32_t first4b =
        buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];

    if (first4b == 0xFFD8FFE0 || first4b == 0xFFD8FFE1 ||
        first4b == 0xFFD8FFEE) {
      data->type = JPG;
      data->mimes[0] = "image/jpeg";
      data->mimes_size = 1;

    } else if (first2b == 0xFFFB || first2b == 0xFFF3 || first2b == 0xFFF2) {
      data->type = MP3;
      data->mimes[0] = "audio/mpeg";
      data->mimes_size = 1;
    }

    if (data->size >= 8) {
      uint32_t second4b =
          buffer[4] << 24 | buffer[5] << 16 | buffer[6] << 8 | buffer[7];

      if (first4b == 0x89504E47 && second4b == 0x0D0A1A0A) {
        data->type = PNG;
        data->mimes[0] = "image/png";
        data->mimes_size = 1;

      } else if (first4b == 0x66747970 &&
                 (second4b == 0x69736F6D || second4b == 0x4D534E56)) {
        data->type = MP4;
        data->mimes[0] = "video/mpeg";
        data->mimes[1] = "video/mp4";
        data->mimes_size = 2;
      }
    }
  }
}

int process_stdin(struct data *data) {
  size_t buffer_size = 1024;
  data->buffer = malloc(buffer_size);

  char ch;
  while ((ch = getchar()) != EOF) {
    if (data->size == buffer_size) {
      buffer_size *= 2;
      char *_buffer = realloc(data->buffer, buffer_size);
      if (!_buffer) {
        __print_error("xdcopy", "failed to realloc %d bytes: \n", buffer_size);
        perror("realloc");
        free(data->buffer);
        return 0;
      }
      data->buffer = _buffer;
    }
    data->buffer[data->size++] = ch;
  }

  return data->size;
}

xdwl_proxy *init(struct data *data) {
  xdwl_proxy *proxy = xdwl_proxy_create();
  if (!proxy) {
    return NULL;
  }
  data->proxy = proxy;

  if (xdwl_object_register(proxy, 1, "wl_display") == 0) {
    return NULL;
  }

  struct xdwl_display_event_handlers wl_display = {
      .delete_id = handle_wl_display_delete_id,
      .error = handle_wl_display_error,
  };

  ENSURE_RESULT(xdwl_display_add_listener(proxy, &wl_display, data));

  size_t wl_registry_id = xdwl_object_register(proxy, 0, "wl_registry");

  struct xdwl_registry_event_handlers wl_registry_event_handlers = {
      .global = handle_wl_registry_global};

  ENSURE_RESULT(
      xdwl_registry_add_listener(proxy, &wl_registry_event_handlers, data));

  xdwl_display_get_registry(proxy, wl_registry_id);
  ROUNDTRIP;

  // DATA CONTROL DEVICE
  xdwl_id zwlr_data_control_device_id =
      xdwl_object_register(proxy, 0, "zwlr_data_control_device_v1");

  if (zwlr_data_control_device_id == 0)
    __abort(data);

  struct xdzwlr_data_control_device_v1_event_handlers wlr_data_control_device =
      {
          .data_offer = handle_wlr_data_control_device_data_offer,
      };

  ENSURE_RESULT(xdzwlr_data_control_device_v1_add_listener(
      proxy, &wlr_data_control_device, data));

  xdwl_object *wl_seat_object = xdwl_object_get_by_name(data->proxy, "wl_seat");
  if (!wl_seat_object)
    __abort(data);

  xdzwlr_data_control_manager_v1_get_data_device(
      proxy, 0, zwlr_data_control_device_id, wl_seat_object->id);
  ROUNDTRIP;

  // DATA CONTROL SOURCE
  xdwl_id zwlr_data_control_source_id =
      xdwl_object_register(proxy, 0, "zwlr_data_control_source_v1");

  if (zwlr_data_control_device_id == 0)
    __abort(data);

  struct xdzwlr_data_control_source_v1_event_handlers wlr_data_control_source =
      {
          .send = handle_wlr_data_control_source_send,
          .cancelled = handle_wlr_data_control_source_cancelled,
      };

  ENSURE_RESULT(xdzwlr_data_control_source_v1_add_listener(
      proxy, &wlr_data_control_source, data));

  xdzwlr_data_control_manager_v1_create_data_source(
      proxy, 0, zwlr_data_control_source_id);

  for (size_t i = 0; i < data->mimes_size; i++) {
    const char *mime = data->mimes[i];
    xdzwlr_data_control_source_v1_offer(data->proxy, 0, mime);
  }

  xdwl_object *wlr_data_control_source_object =
      xdwl_object_get_by_name(data->proxy, "zwlr_data_control_source_v1");

  if (!wlr_data_control_source_object) {
    __abort(data);
  }

  xdzwlr_data_control_device_v1_set_selection(
      data->proxy, 0, wlr_data_control_source_object->id);

  return proxy;
}

int main(int argc, char *argv[]) {
  int opt;
  int option_index;

  char filepath[PATH_MAX];
  int copy_stdin = 1;

  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"file", optional_argument, 0, 'f'},
  };

  while ((opt = getopt_long(argc, argv, "hf:", long_options, &option_index)) !=
         -1) {
    switch (opt) {
    case 'h':
      print_help();
      return 0;

    case 'f':
      if (!realpath(optarg, filepath)) {
        __print_error("xdcopy", "%s does not exist\n", optarg);
        return 1;
      }

      copy_stdin = 0;
      break;

    case '?':
      return 1;
    }
  }

  struct data data = {
      .buffer = NULL,
      .size = 0,
  };

  if (optind != argc) {
    data.size = strlen(argv[1]);
    data.buffer = malloc(data.size);
    if (!data.buffer) {
      __print_error("xdcopy", "failed to malloc\n");
      return 1;
    }
    memcpy(data.buffer, argv[1], data.size);

  } else if (copy_stdin) {
    if ((data.size = process_stdin(&data)) == 0) {
      __print_error("xdcopy", "failed to read stdin\n");
      if (data.buffer)
        free(data.buffer);
      return 1;
    }

  } else {
    data.size = strlen(filepath) + 7 + 1;
    data.buffer = malloc(data.size);
    snprintf(data.buffer, data.size, "file://%s", filepath);
    data.buffer[data.size - 1] = 0;

    data.type = URI;
    data.mimes[0] = "text/uri-list";
    data.mimes_size = 1;
  }

  if (data.type != URI) {
    assign_mimes(&data);
  }

  init(&data);
  if (fork() != 0) {
    __abort(&data);
    return 1;

  } else {
    while (xdwl_dispatch(data.proxy) != -1)
      ;
    __abort(&data);
    return 0;
  }
}
