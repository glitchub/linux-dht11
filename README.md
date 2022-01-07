A simple stand-alone DHT11 driver for linux.

Yes, there is a DHT11 driver in the kernel tree, but it fails if DHT11 returns
fractional results as mine does. Not sure if that's the behavior of an older
version of the device or is just a bug.

This driver has been tested on RPI but should work on any system if the GPIO
contrioller supports edge triggers. Device tree overlays etc are not required.

To build, simply run 'make'. Build tools and kernel headers must be installed.

To start, run 'sudo insmod dht11.ko gpio=X', where X is the number of the GPIO
that the DHT11 is attached to (default is GPIO 4).

Thereafter, reading /dev/dht11 will return a line of text in form "HHH TTT\n",
where HHH is the relatively humidity in tenths of a percent and TTT is the
ambient temperature in tenths of a degree. Note the DHT11 has nominal
temperature range 0 to 50C, negative temperatures are not supported.

If the DHT11 is not functioning then read will fail with -EINVAL instead.
