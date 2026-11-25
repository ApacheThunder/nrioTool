# Starter files

`starter.zip` contains the stock uDisk 1.45 payload, `xmenu.dat`, and four
`DSYSTEM` defaults extracted unchanged from the original nrioTool
`nrio_basicimage_1995712_16g.img` at upstream commit
`4cf70ae13f908e2391cd608f833699d711a7d92a`. Its source image had SHA-256
`c3ad059df41a548ced4ad58623e7eb7f3aa78bd0ab3b17be26c1006aeda44497`.

This is a build input only. The build compresses the separate files into
the existing RAM-resident asset bundle alongside the current XuluMenu
build and the unified DLDI. No sector image or separate ZIP is needed
when running nrioTool, including on DS Lite.
