Summary: Remote Desktop
Name: rdesktop
Version: 1.2.0
Release: 1
Copyright: GPL; see COPYING
Group: Applications/Communications
Source: rdesktop.tgz
BuildRoot: %{_tmppath}/%{name}-buildroot
Packager: Peter Åstrand <peter@cendio.se>
Requires: XFree86-libs 

%description
rdesktop is a client for Microsoft Windows NT Terminal Server, Windows 2000
Terminal Services, Windows XP Remote Desktop, and possibly other Terminal
Services products.  rdesktop currently implements the RDP version 4 protocol.

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

