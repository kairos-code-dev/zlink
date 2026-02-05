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
%define lib_name libzlink5
Name:          zlink
Version:       4.3.5
Release:       1%{?dist}
Summary:       The Zlink messaging library
Group:         Development/Libraries/C and C++
License:       MPL-2.0
URL:           http://www.zlink.org/
Source:        http://download.zlink.org/%{name}-%{version}.tar.gz
Prefix:        %{_prefix}
Buildroot:     %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires:  autoconf automake libtool glib2-devel libbsd-devel
%if ! (0%{?fedora} > 12 || 0%{?rhel} > 5)
BuildRequires:  e2fsprogs-devel
BuildRoot:      %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
%endif
%bcond_with pgm
%if %{with pgm}
BuildRequires:  openpgm-devel
%define PGM yes
%else
%define PGM no
%endif
%bcond_with nss
%if %{with nss}
%if 0%{?suse_version}
BuildRequires:  mozilla-nss-devel
%else
BuildRequires:  nss-devel
%endif
%define NSS yes
%else
%define NSS no
%endif
%bcond_with tls
%if %{with tls} && ! 0%{?centos_version} < 700
%if 0%{?suse_version}
BuildRequires:  libgnutls-devel
%else
BuildRequires:  gnutls-devel
%endif
%define TLS yes
%else
%define TLS no
%endif
BuildRequires: gcc, make, gcc-c++, libstdc++-devel, asciidoc, xmlto
Requires:      libstdc++

%ifarch pentium3 pentium4 athlon i386 i486 i586 i686 x86_64
%{!?_with_pic: %{!?_without_pic: %define _with_pic --with-pic}}
%{!?_with_gnu_ld: %{!?_without_gnu_ld: %define _with_gnu_ld --with-gnu_ld}}
%endif

# We do not want to ship libzlink.la
%define _unpackaged_files_terminate_build 0

%description
The 0MQ lightweight messaging kernel is a library which extends the
standard socket interfaces with features traditionally provided by
specialised messaging middleware products. 0MQ sockets provide an
abstraction of asynchronous message queues, multiple messaging
patterns, message filtering (subscriptions), seamless access to
multiple transport protocols and more.

%package -n %{lib_name}
Summary:   Shared Library for Zlink
Group:     Productivity/Networking/Web/Servers
Conflicts: zlink

%description -n %{lib_name}
The 0MQ lightweight messaging kernel is a library which extends the
standard socket interfaces with features traditionally provided by
specialised messaging middleware products. 0MQ sockets provide an
abstraction of asynchronous message queues, multiple messaging
patterns, message filtering (subscriptions), seamless access to
multiple transport protocols and more.

This package contains the Zlink shared library.

%package devel
Summary:  Development files and static library for the Zlink library
Group:    Development/Libraries
Requires: %{lib_name} = %{version}-%{release}, pkgconfig
%bcond_with pgm
%if %{with pgm}
Requires:  openpgm-devel
%endif
%bcond_with nss
%if %{with nss}
%if 0%{?suse_version}
Requires:  mozilla-nss-devel
%else
Requires:  nss-devel
%endif
%endif
%bcond_with tls
%if %{with tls} && ! 0%{?centos_version} < 700
%if 0%{?suse_version}
Requires:  libgnutls-devel
%else
Requires:  gnutls-devel
%endif
%endif

%description devel
The 0MQ lightweight messaging kernel is a library which extends the
standard socket interfaces with features traditionally provided by
specialised messaging middleware products. 0MQ sockets provide an
abstraction of asynchronous message queues, multiple messaging
patterns, message filtering (subscriptions), seamless access to
multiple transport protocols and more.

This package contains Zlink related development libraries and header files.

%prep
%setup -q

# Sed version number of openpgm into configure
%global openpgm_pc $(basename %{_libdir}/pkgconfig/openpgm*.pc .pc)
sed -i "s/openpgm-[0-9].[0-9]/%{openpgm_pc}/g" \
    configure*

%build
# Workaround for automake < 1.14 bug
mkdir -p config
autoreconf -fi
%configure --enable-drafts=%{DRAFTS} \
    --with-pgm=%{PGM} \
    --with-nss=%{NSS} \
    --with-tls=%{TLS} \
    %{?_with_pic} \
    %{?_without_pic} \
    %{?_with_gnu_ld} \
    %{?_without_gnu_ld}

%{__make} %{?_smp_mflags}

%check
%{__make} check VERBOSE=1

%install
[ "%{buildroot}" != "/" ] && %{__rm} -rf %{buildroot}
# Install the package to build area
%makeinstall

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%clean
[ "%{buildroot}" != "/" ] && %{__rm} -rf %{buildroot}

%files -n %{lib_name}
%defattr(-,root,root,-)

# docs in the main package
%doc AUTHORS LICENSE NEWS

# libraries
%{_libdir}/libzlink.so.*

%{_mandir}/man7/zlink.7.gz

%files devel
%defattr(-,root,root,-)
%{_includedir}/zlink.h
%{_includedir}/zlink_utils.h

%{_libdir}/libzlink.a
%{_libdir}/pkgconfig/libzlink.pc
%{_libdir}/libzlink.so

%{_mandir}/man3/zlink*
# skip man7/zlink.7.gz
%{_mandir}/man7/zlink_*

%changelog
* Fri Oct 4 2019 Luca Boccassi <luca.boccassi@gmail.com>
- Add macro for optional TLS dependency

* Wed Sep 11 2019 Luca Boccassi <luca.boccassi@gmail.com>
- Add macro for optional NSS dependency

* Sat Aug 19 2017 Luca Boccassi <luca.boccassi@gmail.com>
- Fix parsing and usage of conditionals for sodium/pgm/krb5 so that they work
  in OBS
- Do not ship libzlink.la anymore, it's not needed and causes overlinking

* Sun Nov 06 2016 Luca Boccassi <luca.boccassi@gmail.com>
- Add libzlink-tool to package curve_keygen in /usr/bin

* Sun Jul 31 2016 Luca Boccassi <luca.boccassi@gmail.com>
- Follow RPM standards and rename zlink to libzlink5

* Sat Oct 25 2014 Phillip Mienk <mienkphi@gmail.com>
- Add --with/--without libgssapi_krb5 support following J.T.Conklin's pattern

* Sat Oct 18 2014 J.T. Conklin <jtc@acorntoolworks.com>
- Add --with/--without pgm support
- Add --with/--without libsodium support

* Tue Jun 10 2014 Tristian Celestin <tristian.celestin@outlook.com> 4.0.4
- Updated packaged files

* Mon Nov 26 2012 Justin Cook <jhcook@gmail.com> 3.2.2
- Update packaged files

* Fri Apr 8 2011 Mikko Koppanen <mikko@kuut.io> 3.0.0-1
- Update dependencies and packaged files

* Sat Apr 10 2010 Mikko Koppanen <mkoppanen@php.net> 2.0.7-1
- Initial packaging
