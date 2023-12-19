# Requires to set the `portals4_headers` variable

swptl_headers = include_directories('include')

swptl_debug_flags = []
if swptl_debug
  swptl_debug_flags += ['-DDEBUG']
endif

swptl_static = static_library(
  'swptl',
  'swptl.c', 'utils.c', 'ptl_log.c', 'pool.c', 'timo.c', 'ptl_str.c', 'bximsg.c', 'bximsg_wthr.c',
  'bxipkt_udp.c', 'bxipkt_common.c',
  c_args: swptl_debug_flags + [
    '-Wno-discarded-qualifiers',
    '-Wno-missing-prototypes',
  ],
  include_directories: [portals4_headers],
)
