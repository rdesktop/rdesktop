# Automated test suite

An basic test suite was added while implementing the dynamic resize
feature. Improvements to this test suite are very welcome.


## Requirements

In addition to the build requirements of rdesktop itself, you also need:

 * [cgreen](https://github.com/cgreen-devs/cgreen)


## Building and running the tests

    cd tests
    make

This will build and run each test in turn. Re-running `make` will
recompile the tests as necessary, and run them again.

The display-selection tests can also run independently, without Cgreen or a
running X server:

```sh
make -C tests test-xdisplay
```

Run this command from the repository root. It checks XWayland fallback,
preservation of explicit X11 displays, and rejection of sockets owned by
another user or connections rejected by Xlib authentication.

On Linux, after building the client, run the standalone stream and public-key
regression checks without Cgreen or an RDP server:

```sh
make -C tests test-crypto-keys
```

These check stream boundaries and resizing, RSA modulus/exponent extraction,
rejection of invalid and non-RSA keys, and PKCS#1 public-key encoding through
an in-memory TLS client/server connection.


## Cgreen documentation

You can find the Cgreen documentation over
at [their github site](https://cgreen-devs.github.io).
