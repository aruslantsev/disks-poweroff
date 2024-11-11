all: rpm deb

rpm:
	rpmbuild -ba --build-in-place disks-spindown.spec

deb:
    mkdir disks-spindown_0.7-1_all
    mkdir -p disks-spindown_0.7-1_all/usr/bin
    cp disks-spindown.sh disks-spindown_0.7-1_all/usr/bin
    mkdir -p disks-spindown_0.7-1_all/usr/lib/systemd/system
    cp disks-spindown.service disks-spindown_0.7-1_all/usr/lib/systemd/system
    mkdir -p disks-spindown_0.7-1_all/etc
    cp disks-spindown.conf disks-spindown_0.7-1_all/etc
    mkdir disks-spindown_0.7-1_all/DEBIAN
    cp control disks-spindown_0.7-1_all/DEBIAN/control
    dpkg-deb --build --root-owner-group disks-spindown_0.7-1_all
