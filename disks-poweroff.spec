Name:       disks-poweroff
Version:    0.4
Release:    1%{?dist}
Summary:    Stop inactive disks
License:    GPLv3+
BuildArch:  noarch

Requires:   smartmontools
Requires:   hdparm

%define _unitdir /usr/lib/systemd/system

%description
Stop inactive disks after timeout

%install
install -D -m 644 disks-poweroff.service %{buildroot}%{_unitdir}/disks-poweroff.service
install -D -m 644 disks-poweroff.conf %{buildroot}%{_sysconfdir}/disks-poweroff.conf
install -D -m 644 disks-poweroff.conf.example %{buildroot}%{_sysconfdir}/disks-poweroff.conf.example
install -D -m 755 disks-poweroff.py %{buildroot}%{_bindir}/disks-poweroff.py

%files
%{_unitdir}/disks-poweroff.service
%{_sysconfdir}/disks-poweroff.conf.example
%{_bindir}/disks-poweroff.py
%config %{_sysconfdir}/disks-poweroff.conf

%changelog
* Wed Mar 16 2022 Andrei Ruslantsev - 0.4
- Fix typos

* Fri Mar 11 2022 Andrei Ruslantsev - 0.3
- Add dependencies to RPM

* Thu Mar 10 2022 Andrei Ruslantsev - 0.2
- Use system logger

* Thu Mar 10 2022 Andrei Ruslantsev - 0.1
- Initial release
