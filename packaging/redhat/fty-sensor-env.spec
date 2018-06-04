#
#    fty-sensor-env - Grab temperature and humidity data from sensors attached to the box
#
#    Copyright (C) 2014 - 2018 Eaton
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

# To build with draft APIs, use "--with drafts" in rpmbuild for local builds or add
#   Macros:
#   %_with_drafts 1
# at the BOTTOM of the OBS prjconf
%bcond_with drafts
%if %{with drafts}
%define DRAFTS yes
%else
%define DRAFTS no
%endif
%define SYSTEMD_UNIT_DIR %(pkg-config --variable=systemdsystemunitdir systemd)
Name:           fty-sensor-env
Version:        1.0.0
Release:        1
Summary:        grab temperature and humidity data from sensors attached to the box
License:        GPL-2.0+
URL:            https://42ity.org
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
# Note: ghostscript is required by graphviz which is required by
#       asciidoc. On Fedora 24 the ghostscript dependencies cannot
#       be resolved automatically. Thus add working dependency here!
BuildRequires:  ghostscript
BuildRequires:  asciidoc
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  systemd
%{?systemd_requires}
BuildRequires:  xmlto
BuildRequires:  libsodium-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  fty-proto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
fty-sensor-env grab temperature and humidity data from sensors attached to the box.

%package -n libfty_sensor_env1
Group:          System/Libraries
Summary:        grab temperature and humidity data from sensors attached to the box shared library

%description -n libfty_sensor_env1
This package contains shared library for fty-sensor-env: grab temperature and humidity data from sensors attached to the box

%post -n libfty_sensor_env1 -p /sbin/ldconfig
%postun -n libfty_sensor_env1 -p /sbin/ldconfig

%files -n libfty_sensor_env1
%defattr(-,root,root)
%{_libdir}/libfty_sensor_env.so.*

%package devel
Summary:        grab temperature and humidity data from sensors attached to the box
Group:          System/Libraries
Requires:       libfty_sensor_env1 = %{version}
Requires:       libsodium-devel
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       fty-proto-devel

%description devel
grab temperature and humidity data from sensors attached to the box development tools
This package contains development files for fty-sensor-env: grab temperature and humidity data from sensors attached to the box

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libfty_sensor_env.so
%{_libdir}/pkgconfig/libfty_sensor_env.pc
%{_mandir}/man3/*
%{_mandir}/man7/*

%prep

%setup -q

%build
sh autogen.sh
%{configure} --enable-drafts=%{DRAFTS} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md
%{_bindir}/fty-sensor-env
%{_mandir}/man1/fty-sensor-env*
%{SYSTEMD_UNIT_DIR}/fty-sensor-env.service
%dir %{_sysconfdir}/fty-sensor-env
%if 0%{?suse_version} > 1315
%post
%systemd_post fty-sensor-env.service
%preun
%systemd_preun fty-sensor-env.service
%postun
%systemd_postun_with_restart fty-sensor-env.service
%endif

%changelog
