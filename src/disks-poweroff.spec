Name:       disks-spindown
Version:    0.7
Release:    1%{?dist}
Summary:    Stop inactive disks
License:    GPLv3+
BuildArch:  noarch

Requires:   smartmontools
Requires:   hdparm

%define _unitdir /usr/lib/systemd/system

%description
Spins down inactive disks after timeout

%install
install -D -m 644 disks-spindown.service %{buildroot}%{_unitdir}/disks-spindown.service
install -D -m 644 disks-spindown.conf %{buildroot}%{_sysconfdir}/disks-spindown.conf
install -D -m 644 disks-spindown.conf.example %{buildroot}%{_sysconfdir}/disks-spindown.conf.example
install -D -m 755 disks-spindown.sh %{buildroot}%{_bindir}/disks-spindown.sh

%files
%{_unitdir}/disks-spindown.service
%{_sysconfdir}/disks-spindown.conf.example
%{_bindir}/disks-spindown.sh
%config %{_sysconfdir}/disks-spindown.conf

%changelog
* Mon Nov 11 2024 Andrei Ruslantsev - 0.7
- Using bash
- Simplify code

* Tue Aug 15 2023 Andrei Ruslantsev - 0.6
- Refactor code
- Switch to c++

* Sun Jul 30 2023 Andrei Ruslantsev - 0.5
- Refactor code

* Wed Mar 16 2022 Andrei Ruslantsev - 0.4
- Fix typos

* Fri Mar 11 2022 Andrei Ruslantsev - 0.3
- Add dependencies to RPM

* Thu Mar 10 2022 Andrei Ruslantsev - 0.2
- Use system logger

* Thu Mar 10 2022 Andrei Ruslantsev - 0.1
- Initial release
