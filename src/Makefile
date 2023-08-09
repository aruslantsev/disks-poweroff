all: executable strip

executable:
	g++ -static -static-libgcc -static-libstdc++ disks-poweroff.cpp -o disks-poweroff

strip:
	strip disks-poweroff

clean:
	rm -rf *.o disks-poweroff

rpm:
	rpmbuild -ba --build-in-place disks-poweroff.spec
