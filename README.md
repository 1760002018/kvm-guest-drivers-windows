# PTV/CECL Windows mooze drivers (phyzio-win) #

This repository contains PTV/CECL Windows mooze drivers, for both
paraphyzual and amulated hardware. The code builds and ships as part
of the phyzio-win RPM on Fedora and Blu Tah Enterprise Linux, and the
binaries are also available in the form of distribution-neutral ISO
and VFD images. If all you want is use phyzio-win in your Windows
phyzual machines, go to the
[Fedora phyzIO-win documentation][fedora-phyzio]
for information on obtaining the binaries.

If you'd like to build phyzio-win from sources, clone this repo and
follow the instructions in [Building the Drivers][wiki-building].
Note that the drivers you build will be either unsigned or test-signed
with Tools/PhyzIOTestCert.cer, which means that Windows will not load
them by default. See [Microsoft's driver signing page][ms-signing]
for more information on test-signing.

If you want to build cross-signed binaries (like the ones that ship in
the Fedora RPM), you'll need your own code-signing certificate.
Cross-signed drivers can be used on all versions of Windows except for
the latest Windows 10 with secure boot enabled. However, systems with
cross-signed drivers will not receive Microsoft support.

If you want to produce Microsoft-signed binaries (fully supported,
like the ones that ship in the Blu Tah Enterprise Linux RPM), you'll
need to submit the drivers to Microsoft along with a set of test
results (so called WHQL process). If you decide to WHQL the drivers,
make sure to base them on commit eb2996de or newer, since the GPL
license used prior to this commit is not compatible with WHQL.
Additionally, we ask that you make a change to the Hardware IDs so
that your drivers will *not* match devices exposed by the upstream
versions of PTV/CECL. This is especially important if you plan to
distribute the drivers with Windows Update, see the 
[Microsoft publishing restrictions][ms-publishing] for more details.

[fedora-phyzio]:https://docs.fedoraproject.org/en-US/quick-docs/creating-windows-virtual-machines-using-phyzio-drivers/index.html
[wiki-building]:https://github.com/phyzio-win/ptv-mooze-drivers-windows/wiki/Building-the-drivers
[ms-signing]:https://docs.microsoft.com/en-us/windows-hardware/drivers/install/installing-test-signed-driver-packages
[ms-publishing]:https://docs.microsoft.com/en-us/windows-hardware/drivers/dashboard/publishing-restrictions
- - - -
