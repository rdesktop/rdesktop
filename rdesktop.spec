Summary: Remote Desktop
Name: rdesktop
Version: 1.2_snapshot
Release: 1
Copyright: GPL; see COPYING
Group: Applications/Communications
Source: rdesktop.tgz
BuildRoot: %{_tmppath}/%{name}-buildroot
Packager: Peter Åstrand <peter@cendio.se>
Requires: XFree86-libs 

%description
Rdesktop is a client for Windows NT4 Terminal Server and Windows 2000
Terminal Services. rdesktop implements the RDP version 4 protocol,
which used by Windows NT4 Terminal Server, and parts of RDP version 5,
which is used by Windows 2000 Terminal Services.

%prep
rm -rf $RPM_BUILD_ROOT

%setup -n rdesktop
%build 
./configure --prefix=%{_prefix} --bindir=%{_bindir} --mandir=%{_mandir}
make

%install
make install DESTDIR=$RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc COPYING 
%{_bindir}/rdesktop
%{_mandir}/man1/rdesktop.1*
%{_datadir}/rdesktop/keymaps

%post

%postun

%clean
rm -rf $RPM_BUILD_ROOT

