spacefn-evdev
=============

> This is a little tool to implement
> [the SpaceFn keyboard layout](https://geekhack.org/index.php?topic=51069.0)
> on Linux.
> 
> I wanted to try SpaceFn on my laptop, obviously with a built-in keyboard.
> The only previous Linux implementation I could find
> [requires recompiling the Xorg input driver](http://www.ljosa.com/~ljosa/software/spacefn-xorg/),
> which is an impressive effort but is tricky to compile and means restarting my X server every time I want to make a change.


## Requirements

> - libevdev
>    and its headers or -dev packages on some systems
> - uinput
>    `/dev/uinput` must be present and you must have permission to read and write it
>
> You also need permission to read `/dev/input/eventXX`.
>
> On my system all the requisite permissions are granted by making myself a member of the `input` group.
> You can also just run the program as root.


## Fork

### SpaceFN Build on NixOS

build:
```bash
git clone https://github.com/Gediminas/spacefn-evdev
nix-shell -p gcc pkgconfig libevdev
make
````

Add to `configuration.nix`:
```bash
systemd.services.spacefn = {
    enable = true;
    description = "SpaceFn";
    unitConfig = {
        Type = "simple";
    };
    serviceConfig = {
        ExecStart = "/bin/sh /home/gds/sub/spacefn-evdev/space";
    };
    wantedBy = [ "multi-user.target" ];
};

```

Development:
```bash
nix-shell -p gcc pkgconfig libevdev
make && sudo systemctl restart spacefn.service
```

