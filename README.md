# libtscb [![Build Status](https://travis-ci.org/afett/libtscb.svg?branch=afe-maintenance)](https://travis-ci.org/afett/libtscb)
Thread Save Callback Library by Helge Bahmann

* branch master is a mirror of https://github.com/caleridas/libtscb
* branch afe-maintenance is only on https://github.com/afett/libtscb

## Building

    $ autoreconf --install
    $ ./configure
    $ make
    $ make install

Use the `--enable-shared` switch of the `configure` script to build a shared lib.

## Portability

While the original project supports a range of platforms the maintenance
branch is only tested on recent Linux systems on x86_64.
