# Requires to set the `portals4_headers` variable

swptl_headers = include_directories('include')

swptl_static = static_library(
  'swptl',
  'swptl.c', 'utils.c', 'ptl_log.c', 'pool.c', 'timo.c', 'ptl_str.c', 'bximsg.c', 'bximsg_wthr.c',
  'bxipkt_udp.c', 'bxipkt_common.c',
  include_directories: [portals4_headers],
)
