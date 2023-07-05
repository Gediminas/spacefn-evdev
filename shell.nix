#! /usr/bin/env nix-shell

with import <nixpkgs> { };
mkShell {
  name = "nlx-dkms";

  buildInputs = [
    figlet

    gcc pkgconfig libevdev

    gnumake
    quilt
  ];

  shellHook = ''
    echo --------------------------------------------
    figlet "$name"
    echo "[C]"
    gcc --version | grep gcc
    echo --------------------------------------------
  '';
}
