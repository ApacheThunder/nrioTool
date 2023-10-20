# nrioTool
A tool for dumping hidden SRL rom sections from N-Card and it's clones. (F-Card, Fire Card, DS-Linker and Neoflash Mk5 are known clones of N-Card)

Currently write support will not be implemented until proper DLDI drivers are made for these carts. (and that a write version of the B7 read command is found)

The publically known DLDI driver does not actually
appear to work...at least for modern homebrew. The actual DLDI driver xmenu patches into homebrew has been dump and is currently being reverse engineered.
It is a 19kb driver and thus is using the old 32KB DLDI spec which is not compatible with newer homebrew. Please wait while we work on fixing that.

Write support for primary rom setion is not planned as it's not known if bricking it will result in a card that can still be accessed by this tool and the cart
has to be bootable for the udisk stuff to work. (the main rom SRL also does not appear to be accessible with the normal B7 cart commands used for the others anyways.
It might not be possible to replace it)

Stage2/udisk can probably be replaced safely in the future but is highly recommend you keep backups of the originals.
If those are bricked only this tool can recover them as the cart will not be in a bootable state!

Credits for improvements to file read logic go to lifehackerhansol.