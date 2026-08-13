local total = host.collect({
    { amount = 2 },
    { amount = 5 },
})

assert(total == 7)
