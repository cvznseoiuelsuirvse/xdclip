project(
  'xdclip',
  'c',
  default_options: [
    'optimization=3',
  ],
)

cc = meson.get_compiler('c')
scanner = find_program('xdwayland-scanner', required: true)

xdwayland_dep = dependency('xdwayland', required: true)

protocol_xml = files('protocols/wlr-data-control-unstable-v1.xml')
protocol_sources = custom_target(
  'wlr-data-control-unstable-v1-protocol',
  input: protocol_xml,
  output: [
    'wlr-data-control-unstable-v1-protocol.c',
    'wlr-data-control-unstable-v1-protocol.h',
  ],
  command: [scanner, '@INPUT@', 'wlr-data-control-unstable-v1-protocol'],
)

xdcopy_sources = files(
  './src/util.c',
  './src/xdcopy.c',
)

xdpaste_sources = files(
  './src/util.c',
  './src/xdpaste.c',
)

executable(
  'xdcopy',
  [xdcopy_sources, protocol_sources],
  protocol_sources,
  dependencies: xdwayland_dep,
  install: true,
)

executable(
  'xdpaste',
  [xdpaste_sources, protocol_sources],
  protocol_sources,
  dependencies: xdwayland_dep,
  install: true,
)
