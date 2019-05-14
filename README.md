# CartInstall
An uncreatively named __[GodMode9](https://github.com/d0k3/GodMode9)__ fork for installing 3ds cartridge games to the SD

## General Info
As stated above, this is merely a fork of [GodMode9](https://github.com/d0k3/GodMode9). I played no part in the creation of any of GodMode9.
Installing cartridges to the SD this way bypasses the need to have 2 times as much space free as the game actually takes, and is about % faster than the traditional method of converting the cartridge image to a cia, then installing that with FBI.
This tool is very far from being thouroughly tested, so if you encounter any problems, please submit them to the issue tracker.

## Usage
Download `cartInstall.firm` from the [releases page](https://github.com/aspargas2/cartInstall/releases) and run it with the chainloader of your choice.
Installation will not be finalized until you boot into a luma version that includes rosalina.
If you reset luma's `config.bin` before booting into luma version that includes rosalina, the installation will not be finalized automatically. If you have accidentally done this, attempting to open the homebrew launcher through rosalina should finalize the installation and reboot.
Leaving an installation unfinalized means the game will take up space on your sd, but the 3ds will not think it is installed.

This tool currently only works with sysnand.

If you somehow manage to install this tool to your firm0, you should first give yourself a solid facepalm. Then put [safeb9sinstaller](https://github.com/d0k3/safeb9sinstaller/releases)'s `.firm` on the sd root as `iderped.firm` and [boot9strap](https://github.com/SciresM/boot9strap/releases)'s `boot9strap.firm` and `boot9strap.firm.sha` in a `boot9strap` folder on the sd root, and boot the console. You should be in safeb9sinstaller, you doofus.

## How to build this / developer info
Build `cartInstall.firm` by running `build.bat`. This requires [firmtool](https://github.com/TuxSH/firmtool), [Python 3.5+](https://www.python.org/downloads/) and [devkitARM](https://sourceforge.net/projects/devkitpro/) installed.
Simply running `make` will build a normal-looking GodMode9. There is no use to doing this, and some scripting may be broken.

For additional customization, you may choose the internal font by replacing `font_default.pbm` inside the `data` directory.
You may also hardcode the brightness via `make FIXED_BRIGHTNESS=x`, whereas `x` is a value between 0...15.

## License
You may use this under the terms of the GNU General Public License GPL v2 or under the terms of any later revisions of the GPL. Refer to the provided `LICENSE.txt` file for further information.


## Credits:
* Everyone who contributed in any way to [GodMode9](https://github.com/d0k3/GodMode9), as this is just a fork of it
* [3DBrew](https://www.3dbrew.org/wiki/Main_Page) and all its editors, for being an essential resource in making this tool
* [@BpyH64](https://github.com/BpyH64) on GitHub for figuring out the CMACs on the `.cmd` files (this tool would not be possible without that discovery), and for the idea of creating dummy CIA files to get the correct entries into ticket.db and title.db