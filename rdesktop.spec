Summary: Remote Desktop Protocol client
Name: rdesktop
Version: 1.8.3post
Release: 1
License: GPL; see COPYING
Group: Applications/Communications
Source: rdesktop-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-buildroot
Packager: Peter Åstrand <astrand@cendio.se>

%description
rdesktop is a client for Remote Desktop Protocol (RDP), used in a number of
Microsoft products including Windows NT Terminal Server, Windows 2000 Server,
Windows XP, Windows 2003 Server and Windows 2008r2.

%prep
rm -rf $RPM_BUILD_ROOT

%setup
%build 
./configure --prefix=%{_prefix} --bindir=%{_bindir} --mandir=%{_mandir}
make

%install
make install DESTDIR=$RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc COPYING doc/AUTHORS doc/keymapping.txt doc/keymap-names.txt doc/ipv6.txt doc/ChangeLog
%{_bindir}/rdesktop
%{_mandir}/man1/rdesktop.1*
%{_datadir}/rdesktop/keymaps

%clean
rm -rf $RPM_BUILD_ROOT

