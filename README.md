<p>
    <h1 align="center"> i3-gradients</h1>
    <img src="screenshot.png">
    <div align="center"><b><i>It's got style, originality and lots of flair!</i></b>
</p>

i3 with highly customizable gradient titlebars, dithering effects, and more coming soon!

# Examples

<img src="screenshot2.png">

<img src="screenshot3.png">

# Setup
</div>

On **Arch-based distros** (including Manjaro, EndeavourOS, Artix, etc.) with access to the **[Arch User Repository](https://aur.archlinux.org/)**,  you can install i3-gradients via the package [i3-gradients-git](https://aur.archlinux.org/packages/i3-gradients-git).

The easiest way to do this is via an AUR helper, such as [yay](https://github.com/Jguer/yay):

```
yay -Sy i3-gradients-git
```

On **non-Arch platforms**, you can install i3-gradients via the following procedure:

* consult the [PKGBUILD](PKGBUILD) and install the packages listed under `depends` and `makedepends`
* `git clone https://github.com/RedBeesRGD/i3-gradients.git`
* `cd i3-gradients && mkdir build`
* `meson setup build/`
* `cd build && sudo meson install`

This will install i3-gradients at the system level (in `/usr/bin`). You can also run `meson compile` to generate an i3-gradients binary in the `build` folder without installing it.

**Installing i3-gradients at the system level conflicts with mainline i3 due to various shared filenames. The AUR package requires that you remove i3 before installing i3-gradients, and it is recommended that you do this if you install via `meson install`.**

<br>

**After installation**:


**Xinit users** can modify their [xinit](https://wiki.archlinux.org/title/Xinit) script or `startx` command to launch the `i3-gradients` binary instead of `i3`.

**Display manager users (KDE, GNOME, etc. - currenly tested with KDE's SDDM)**  should have i3-gradients appear as an option on their login screen, as long as the AUR package or `meson install` were used.

<div align="center"><h1>Configuration</h1></div>

i3-gradients uses its own configuration file. The locations are the same as stock i3 but with `i3` changed to `i3-gradients`, such as `~/.config/i3-gradients/config`. The format is the same as the regular i3 configuration file with a few new options added.

* Toggle gradients: `gradients on/off`
* Toggle dithering: `dithering on/off`
* Gradient colors: `client.gradient_start/end #(hex color)`
* Unfocused window gradient colors: `client.gradient_unfocused_start/end #(hex color)`
* Gradient width: `client.gradient_offset_start/end (number)`(floating-point number between 0 and 1 - default is 0 for start and 1 for end) - **this currently only works if dithering is disabled**
* Dithering level: `dither_noise (number)` (floating-point number, recommended range 0-1, default is 0.5)

By default, i3-gradients will generate a new configuration file in `~/.config/i3-gradients/config` on first run, with defaults for each of the gradient options added at the end. Feel free to modify these to your liking, or replace it with your existing i3 config file and add back the gradient options (we may add a feature to automate this process soon). The defaults will also be used even if they are not specified in the config file.

As with most i3 configuration options, you can see your changes immediately after saving the config by reloading with `$mod+Shift+c`.

<div align="center"><h1>For users new to i3</h1></div>


i3 can be challenging for new users - there are many resources already online on how to configure and use it. Here are our basic recommendations for people who wish to try out i3:

* Install+configure `i3status` and `dmenu`
* Adjust the configuration file to map `$mod+Return` to the terminal of your choice
* Use a program like `feh` to set a desktop wallpaper, if desired
* Learn the basics of i3's keyboard commands and the i3 keybind configuration - the [i3 reference card](https://i3wm.org/docs/refcard.html) may be useful.

<div align="center"><h1>Future plans</h1></div>

Here are a few features we plan to (hopefully) eventually add, ultimately extending this project into a general visual enhancements fork of i3:

* Config item for adjusting the number of colors used by the dithering algorithm
* Adding support for offset control in dithered mode & adding offset control for the dithering itself
* Options to apply gradients to more elements, such as window borders and the i3bar
* Further window decorations such as themable window control buttons & textures
* Glass transparency (Aero-esque)
* Moving gradients
* Rounded corners
* Select different gradients for different windows/window groups
* Sway version

<div align="center"><h1>Contributions</h1></div>

PRs and issues are gratefully accepted. The license is the BSD license used by i3 itself. The repo owner can be contacted on Discord `@redbees`.