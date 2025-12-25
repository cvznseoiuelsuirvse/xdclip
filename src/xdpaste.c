#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xdwayland-client.h>
#include <xdwayland-core.h>
#include <xdwayland-version.h>

#include "../wlr-data-control-unstable-v1-protocol.h"
#include "util.h"

#define ENSURE_RESULT(n)                                                       \
  if ((n) == -1)                                                               \
  __abort(data, 1)

#define ROUNDTRIP ENSURE_RESULT(xdwl_roundtrip(data->proxy))

struct data {
  char *buffer;
  size_t size;
  int rfd;
  int wfd;
  char *mime;
  int add_newline;
  xdwl_proxy *proxy;
};

void print_help() {
  printf("usage: xdpaste [-h] [-n]\n");
  printf("  -h, --help      show this message\n");
  printf("  -n, --newline   add newline to the end. default is no\n");
  printf("\n");
}

void __abort(struct data *data, int exitcode) {
  xdwl_error_print();
  if (data->proxy)
    xdwl_proxy_destroy(data->proxy);
  if (data->mime)
    free(data->mime);
  exit(exitcode);
}

int get_data(struct data *data) {
  size_t buffer_size = 0x1000;
  data->buffer = malloc(buffer_size);

  while (1) {
    if (data->size == buffer_size) {
      buffer_size *= 2;
      char *_buffer = realloc(data->buffer, buffer_size);
      if (!_buffer) {
        __print_error("xdpaste", "failed to realloc %d bytes\n", buffer_size);
        perror("realloc");
        free(data->buffer);
        return 0;
      }
      data->buffer = _buffer;
    }
    int n = read(data->rfd, data->buffer + data->size, 0x1000);
    if (n > 0) {
      data->size += n;

    } else if (n == 0) {
      return data->size;

    } else {
      __print_error("xdpaste", "failed to read\n", buffer_size);
      perror("read");
      free(data->buffer);
      return 0;
    }
  }
}

void handle_wl_display_delete_id(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  size_t o_id = args[1].u;

  if ((xdwl_object_unregister(proxy, o_id)) == -1) {
    __print_error("xdpaste", "Failed to unregister %d\n", o_id);
    __abort(data, 1);
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

  __print_error("xdpaste", "%s#%d (code %d): %s\n", object_name, object_id,
                code, message);
  __abort(data, 1);
}

void handle_wlr_data_control_device_selection(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  int fildes[2];
  if (pipe(fildes) == -1) {
    __print_error("xdpaste", "Failed to pipe()\n");
    goto out;
  };

  data->rfd = fildes[0];
  data->wfd = fildes[1];

  xdzwlr_data_control_offer_v1_receive(data->proxy, 0, data->mime, data->wfd);
  close(data->wfd);

  if (get_data(data) == 0)
    goto out;
  ;

  if (write(1, data->buffer, data->size) == -1) {
    __print_error("xdpaste", "Failed to write to stdout\n");
  };
  if (data->add_newline)
    write(1, "\n", 1);

out:
  close(data->rfd);
  __abort(data, 0);
}

void handle_wlr_data_control_offer_offer(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  const char *mime = args[1].s;

  if (!data->mime) {
    data->mime = strdup(mime);
  }
}

void handle_wlr_data_control_device_data_offer(void *userdata, xdwl_arg *args) {
  struct data *data = userdata;
  xdwl_proxy *proxy = data->proxy;

  xdwl_id new_id = args[1].u;

  xdwl_id zwlr_data_control_offer_id =
      xdwl_object_register(proxy, new_id, "zwlr_data_control_offer_v1");
  if (zwlr_data_control_offer_id == 0)
    __abort(data, 1);

  struct xdzwlr_data_control_offer_v1_event_handlers wlr_data_control_offer = {
      .offer = handle_wlr_data_control_offer_offer,
  };

  ENSURE_RESULT(xdzwlr_data_control_offer_v1_add_listener(
      proxy, &wlr_data_control_offer, data));
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
      __abort(data, 1);
    }

    xdwl_registry_bind(proxy, 0, name, interface, version, object_id);
    ROUNDTRIP;
  }
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
    __abort(data, 1);

  struct xdzwlr_data_control_device_v1_event_handlers wlr_data_control_device =
      {
          .data_offer = handle_wlr_data_control_device_data_offer,
          .selection = handle_wlr_data_control_device_selection,
      };

  ENSURE_RESULT(xdzwlr_data_control_device_v1_add_listener(
      proxy, &wlr_data_control_device, data));

  xdwl_object *wl_seat_object = xdwl_object_get_by_name(data->proxy, "wl_seat");
  if (!wl_seat_object)
    __abort(data, 1);

  xdzwlr_data_control_manager_v1_get_data_device(
      proxy, 0, zwlr_data_control_device_id, wl_seat_object->id);
  ROUNDTRIP;

  return proxy;
}

int main(int argc, char *argv[]) {
  int opt;
  int option_index;
  struct data data = {.mime = NULL, .size = 0, .add_newline = 0};

  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"newline", no_argument, 0, 'n'},
  };

  while ((opt = getopt_long(argc, argv, "hn", long_options, &option_index)) !=
         -1) {
    switch (opt) {
    case 'h':
      print_help();
      return 0;
    case 'n':
      data.add_newline = 1;
    }
  }

  init(&data);

  while (xdwl_dispatch(data.proxy) != -1)
    ;
  __abort(&data, 0);
  return 0;
}
