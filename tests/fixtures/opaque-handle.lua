local handle = host.handle()
assert(host.consume(handle))

local request = { tool = handle }
assert(host.consume_table(request))
