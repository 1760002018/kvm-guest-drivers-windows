@echo off
for %%D in (PhyzIO NetPTV piostor pioscsi Balloon pioserial piorng pioinput piofs pvpanic pciserial fwcfg packaging Q35 ivshmem fwcfg64 piosock piogpu) do (
  pushd %%D
  if exist cleanall.bat (
    call cleanall.bat
  ) else (
    call clean.bat
  )
  popd
)

if exist buildfre_*.log del buildfre_*.log
if exist buildchk_*.log del buildchk_*.log
