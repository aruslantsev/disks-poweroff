all: rpm deb

rpm:
	rpmbuild -ba --build-in-place disks-spindown.spec

version != grep --color=never Version control | cut -d\  -f2
deb:
	rm -r disks-spindown_$(version)-1_all
	mkdir disks-spindown_$(version)-1_all
	mkdir -p disks-spindown_$(version)-1_all/usr/bin
	cp disks-spindown.sh disks-spindown_$(version)-1_all/usr/bin
	mkdir -p disks-spindown_$(version)-1_all/usr/lib/systemd/system
	cp disks-spindown.service disks-spindown_$(version)-1_all/usr/lib/systemd/system
	mkdir -p disks-spindown_$(version)-1_all/etc
	cp disks-spindown.conf disks-spindown_$(version)-1_all/etc
	mkdir disks-spindown_$(version)-1_all/DEBIAN
	cp control disks-spindown_$(version)-1_all/DEBIAN/control
	dpkg-deb --build --root-owner-group disks-spindown_$(version)-1_all
