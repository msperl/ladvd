
# http://fedoraproject.org/wiki/Packaging:RPMMacros
# http://en.opensuse.org/Packaging/RPM_Macros
# http://www.rpm.org/wiki/Problems/Distributions

%global homedir /var/run/ladvd
%global gecos LLDP/CDP sender for unix

%global devel_release		1
#global static_libevent		0

%if 0%{?devel_release}
%global name_suffix	-unstable
%endif
%if 0%{?static_libevent}
%global name_suffix	-static
%global	configure_args	--enable-static-libevent
%endif

Name:		ladvd
BuildRequires:  libevent-devel
%if 0%{?fedora} >= 12
BuildRequires:  libcap-ng-devel
%else
BuildRequires:  libcap-devel
%endif
BuildRequires:  pciutils-devel
BuildRequires:  pkgconfig
BuildRequires:  check-devel
Requires:	/usr/bin/lsb_release
%if ! 0%{?suse_version}
Requires:	hwdata
%endif
Version:	0.9.2
Release:	1
License:	ISC
URL:		http://code.google.com/p/ladvd/
Source0:	%{name}-%{version}.tar.gz
Source1:        %{name}.init
Source2:        %{name}.sysconfig
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Summary:        Main package
Group:          None
%description
None


%package -n %{name}%{?name_suffix}
Summary:        LLDP/CDP sender for unix 
Group:          Productivity/Networking/System
%description -n %{name}%{?name_suffix}
ladvd uses cdp / lldp frames to inform switches about connected hosts,
which simplifies ethernet switch management. It does this by creating
a raw socket at startup, and then switching to a non-privileged user
for the remaining runtime. Every 30 seconds it will transmit CDP/LLDP packets
reflecting the current system state. Interfaces (bridge, bonding,
wireless), capabilities (bridging, forwarding, wireless) and addresses (IPv4,
IPv6) are detected dynamically.


%prep
%setup -q -n %{name}-%{version}


%build
%configure --docdir=%{_docdir}/%{name} %{?configure_args}
make %{?_smp_mflags}


%check
make check


%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} install-strip
rm -rf %{buildroot}%{_docdir}/%{name}
install -D -m 755 %{SOURCE1} %{buildroot}%{_initrddir}/%{name}
%if 0%{?suse_version}
    install -D -m 0644 %{SOURCE2} %{buildroot}/var/adm/fillup-templates/sysconfig.%{name}
%else
    install -D -m 644 %{SOURCE2} %{buildroot}%{_sysconfdir}/sysconfig/%{name}
%endif
mkdir -p %{buildroot}%{homedir}


%clean
rm -rf %{buildroot}


%pre -n %{name}%{?name_suffix}
/usr/sbin/groupadd -r %{name} &>/dev/null || :
/usr/sbin/useradd  -r -s /sbin/nologin -d %{homedir} -M \
    -c '%{gecos}' -g %{name} %{name} &>/dev/null || :


%post -n %{name}%{?name_suffix}
%if ! 0%{?suse_version}
/sbin/chkconfig --add %{name}
%else
%fillup_and_insserv %{name}
%restart_on_update %{name}
%endif


%preun -n %{name}%{?name_suffix}
%if ! 0%{?suse_version}
if [ "$1" = "0" ]; then
	/sbin/service %{name} stop >/dev/null 2>&1 || :
	/sbin/chkconfig --del %{name}
fi
%else
%stop_on_removal %{name}
%endif


%postun -n %{name}%{?name_suffix}
%if ! 0%{?suse_version}
if [ "$1" -ge "1" ]; then
	/sbin/service %{name} condrestart >/dev/null 2>&1 || :
fi
/usr/sbin/userdel %{name} >/dev/null 2>&1 || :
/usr/sbin/groupdel %{name} >/dev/null 2>&1 || :
%else
%{insserv_cleanup}  
%endif


%files -n %{name}%{?name_suffix}
%defattr(-,root,root)
%doc doc/ChangeLog doc/README doc/LICENSE doc/TODO doc/HACKING
%if 0%{?suse_version}
/var/adm/fillup-templates/sysconfig.%{name}
%else
%config(noreplace) %{_sysconfdir}/sysconfig/%{name}
%endif
%{_initrddir}/%{name}
%{_sbindir}/%{name}
%{_sbindir}/%{name}c
%{_mandir}/man8/%{name}.8*
%{_mandir}/man8/%{name}c.8*
%attr(755,root,root) %dir %{homedir}


%changelog
* Sat Feb 20 2010 sten@blinkenlights.nl
- added ladvdc, check-devel and libcap-devel
* Tue Jan 26 2010 sten@blinkenlights.nl
- packaged ladvd version 0.8.6 using the buildservice spec file wizard
