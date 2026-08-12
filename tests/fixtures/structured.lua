assert(host.profile == "debug")
assert(host.invert(false))

local result = host.configure({
    package = "demo",
    enabled = true,
    values = {
        NAME = "demo",
        ABI = 3,
        TRACE = false,
    },
})

assert(result.output == "demo")
assert(result.changed == true)
