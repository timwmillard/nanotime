# Nano Time

Single header file for nano time function in C.  Base from the Go time package.

## Usage

Copy `nanotime.h` into your project source code.

```c
#define NANOTIME_IMPLEMENTATION
#include "nanotime.h"
```

### Change Prefix

Currenlty `nanotime.h` uses `nt_` as the prefix.  If you would like to use
something different, simple do a search and replace for `nt_`.

Use `nanotime_` as prefix.
```sh
sed -i -e 's/nt_/nanotime_/g' nanotime.h
```

Use `time_` as prefix. Similar to the Go package.
```sh
sed -i -e 's/nt_/time_/g' nanotime.h
```

Or just use no prefix.
```sh
sed -i -e 's/nt_//g' nanotime.h
```

