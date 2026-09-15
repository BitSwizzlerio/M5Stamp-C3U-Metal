# The RMT peripheral makes the LED's pulses, so the bit-banging code isn't built.
list(REMOVE_ITEM SOURCES drivers/sk6812.S)
