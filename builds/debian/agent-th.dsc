Format:         1.0
Source:         agent-th
Version:        0.0.0-1
Binary:         libth0, agent-th-dev
Architecture:   any all
Maintainer:     John Doe <John.Doe@example.com>
Standards-Version: 3.9.5
Build-Depends: bison, debhelper (>= 8),
    pkg-config,
    automake,
    autoconf,
    libtool,
    libsodium-dev,
    libzmq4-dev,
    libczmq-dev,
    libmlm-dev,
    libbiosproto-dev,
    dh-autoreconf

Package-List:
 libth0 deb net optional arch=any
 agent-th-dev deb libdevel optional arch=any

