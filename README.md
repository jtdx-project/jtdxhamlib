# jtdxhamlib

Modified hamlib for jtdx. \
For building: \
$ mkdir ~/hamlib-prefix \
$ cd ~/hamlib-prefix \
$ git clone git://github.com/jtdx-project/jtdxhamlib src \
$ cd src \
$ ./bootstrap \
$ mkdir ../build \
$ cd ../build \
$ ../src/configure --prefix=$HOME/hamlib-prefix \
   --disable-shared --enable-static \
   --without-cxx-binding --disable-winradio \
   CFLAGS="-g -O2 -fdata-sections -ffunction-sections" \
   LDFLAGS="-Wl,--gc-sections" \
$ make \
$ make install-strip \
