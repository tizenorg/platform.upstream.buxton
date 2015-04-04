Name:           buxton
Version:        5
Release:        0
License:        LGPL-2.1+
Summary:        A security-enabled configuration system
Url:            https://github.com/sofar/buxton
Group:          System/Configuration
Source0:        %{name}-%{version}.tar.xz
Source1:        tizen.conf
Source1001:     %{name}.manifest
BuildRequires:  libattr-devel
BuildRequires:  gdbm-devel
BuildRequires:  pkgconfig(check)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(libsystemd-daemon)
BuildRequires:  pkgconfig(cynara-client-async)
Requires(post): buxton
Requires(post): smack
Requires(post): /usr/bin/getent
Requires(post): /usr/bin/chown
Requires(post): /usr/sbin/useradd
Requires(post): /usr/sbin/groupadd
Requires(post): /usr/bin/chsmack

%description
Buxton is a security-enabled configuration management system. It
features a layered approach to configuration storage, with each
layer containing an arbitrary number of groups, each of which may
contain key-value pairs.  Mandatory Access Control (MAC) is
implemented at the group level and at the key-value level.

Buxton provides a C library (libbuxton) for client applications to
use.  Internally, buxton uses a daemon (buxtond) for processing
client requests and enforcing MAC. Also, a CLI (buxtonctl) is
provided for interactive use and for use in shell scripts.

%package devel
Summary: A security-enabled configuration system - development files
Requires: %{name} = %{version}

%description devel
Buxton is a security-enabled configuration management system. It
features a layered approach to configuration storage, with each
layer containing an arbitrary number of groups, each of which may
contain key-value pairs.  Mandatory Access Control (MAC) is
implemented at the group level and at the key-value level.

Buxton provides a C library (libbuxton) for client applications to
use.  Internally, buxton uses a daemon (buxtond) for processing
client requests and enforcing MAC. Also, a CLI (buxtonctl) is
provided for interactive use and for use in shell scripts.

This package provides development files for Buxton.

%package -n buxtonsimple
Summary: Simplified buxton API
Requires: %{name} = %{version}

%description -n buxtonsimple
Buxton is a security-enabled configuration management system. It
features a layered approach to configuration storage, with each
layer containing an arbitrary number of groups, each of which may
contain key-value pairs.  Mandatory Access Control (MAC) is
implemented at the group level and at the key-value level.

Buxton-simple provides a simplified C library (libbuxtonsimple)
for simple client applications.

%package -n buxtonsimple-devel
Summary: Simplified buxton API - development files
Requires: %{name} = %{version}

%description -n buxtonsimple-devel
Buxton is a security-enabled configuration management system. It
features a layered approach to configuration storage, with each
layer containing an arbitrary number of groups, each of which may
contain key-value pairs.  Mandatory Access Control (MAC) is
implemented at the group level and at the key-value level.

Buxton-simple provides a simplified C library (libbuxtonsimple)
for simple client applications.

This package provides development files for BuxtonSimple.

%prep
%setup -q
cp %{SOURCE1001} .

%build
%configure
make %{?_smp_mflags}

%install
%make_install
# TODO: need to define needed layers for Tizen in tizen.conf
install -m 0644 %{SOURCE1} %{buildroot}%{_sysconfdir}/buxton.conf

%post
/sbin/ldconfig

# the database directory
dbdir="%{_localstatedir}/lib/buxton"

# buxtond runs as user buxton of group buxton
# create it on need!
getent group buxton > /dev/null || groupadd -r buxton
getent passwd buxton > /dev/null || useradd -r -g buxton -d "${dbdir}" buxton

# create initial databases
buxtonctl create-db base
buxtonctl create-db isp

# The initial DBs will not have the correct labels and
# permissions when created in postinstall during image
# creation, so we set these file attributes here.
chown -R buxton:buxton "${dbdir}"
chsmack -a System "${dbdir}" "${dbdir}"/*.db

%postun -p /sbin/ldconfig

%docs_package
#%license docs/LICENSE.MIT

%files
%manifest %{name}.manifest
#%license LICENSE.LGPL2.1
%config(noreplace) %{_sysconfdir}/buxton.conf
%{_bindir}/buxtonctl
%{_libdir}/buxton/*.so
%{_libdir}/libbuxton.so.*
%{_prefix}/lib/systemd/system/buxton.service
%{_prefix}/lib/systemd/system/buxton.socket
%{_prefix}/lib/systemd/system/sockets.target.wants/buxton.socket
%{_sbindir}/buxtond
%attr(0700,buxton,buxton) %dir %{_localstatedir}/lib/buxton

%files devel
%manifest %{name}.manifest
%{_includedir}/buxton.h
%{_libdir}/libbuxton.so
%{_libdir}/pkgconfig/buxton.pc


%files -n buxtonsimple
%manifest %{name}.manifest
#%license LICENSE.LGPL2.1
%{_libdir}/libbuxtonsimple.so.*

%files -n buxtonsimple-devel
%manifest %{name}.manifest
%{_includedir}/buxtonsimple.h
%{_libdir}/libbuxtonsimple.so
%{_libdir}/pkgconfig/buxtonsimple.pc

