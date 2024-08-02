subdir('hello')

subdir('get_matching')

init_queue = executable('init_queue', 'init_queue.c', dependencies: portals_dep)

put_to_self = executable(
  'put_to_self',
  'put_to_self.c',
  dependencies: portals_dep,
)

search = executable('search', 'search.c', dependencies: portals_dep)

get_id = executable('get_id', 'get_id.c', dependencies: portals_dep)

put_to_self_logical = executable(
  'put_to_self_logical',
  'put_to_self_logical.c',
  dependencies: portals_dep,
)

ping_pong = executable('ping_pong', 'ping_pong.c', dependencies: portals_dep)

reduce = executable('reduce', 'reduce.c', dependencies: portals_dep)

hello = find_program('hello.sh')

get_matching = find_program('get_matching.sh')

test('init_queue', init_queue, is_parallel: false)
test('put_to_self', put_to_self, is_parallel: false)
test('search', search, is_parallel: false)
test('get_id', get_id, is_parallel: false)
test('put_to_self_logical', put_to_self_logical, is_parallel: false)
test('ping_pong', ping_pong, is_parallel: false, args: ['1'])
test('reduce', reduce, is_parallel: false)
test('hello', hello, is_parallel: false)
test('get_matching', get_matching, is_parallel: false)
