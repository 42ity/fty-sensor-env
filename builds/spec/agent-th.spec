#
#    agent-th - Grab temperature and humidity data from some-sensor
#
#    Code in this repository is part of Eaton Intelligent Power Controller SW suite
#
#    Copyright (C) 2015 Eaton
#
#    The software that accompanies this License is the property of Eaton
#    and is protected by copyright and other intellectual property law.
#
#    Final content under discussion...
#    Refer to http://pqsoftware.eaton.com/explore/eng/ipp/license_en.htm?lang=en&file=install/win32/ipp/ipp_win_1_42_109.exe&os=WIN&typeOs=
#

Name:           agent-th
Version:        0.0.0
Release:        1
Summary:        grab temperature and humidity data from some-sensor
License:        MIT
URL:            http://example.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  systemd-devel
BuildRequires:  gcc-c++
BuildRequires:  libsodium-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  biosproto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
agent-th grab temperature and humidity data from some-sensor.

%package -n libth0
Group:          System/Libraries
Summary:        grab temperature and humidity data from some-sensor

%description -n libth0
agent-th grab temperature and humidity data from some-sensor.
This package contains shared library.

%post -n libth0 -p /sbin/ldconfig
%postun -n libth0 -p /sbin/ldconfig

%files -n libth0
%defattr(-,root,root)
%doc LICENSE
%{_libdir}/libth.so.*

%package devel
Summary:        grab temperature and humidity data from some-sensor
Group:          System/Libraries
Requires:       libth0 = %{version}
Requires:       libsodium-devel
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       biosproto-devel

%description devel
agent-th grab temperature and humidity data from some-sensor.
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libth.so
%{_libdir}/pkgconfig/libth.pc

%prep
%setup -q

%build
sh autogen.sh
%{configure} --with-systemd
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc LICENSE
%{_bindir}/agent-th
%{_prefix}/lib/systemd/system/agent-th*.service


%changelog
