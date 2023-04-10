# SPDX-FileCopyrightText: Copyright (c) 2022 by Rivos Inc.
# SPDX-FileCopyrightText: Copyright (c) 2003-2022 Eelco Dolstra and the Nixpkgs/NixOS contributors
# Licensed under the MIT License, see LICENSE for details.
# SPDX-License-Identifier: MIT
{
  description = "gcc (rivos)";

  inputs = {
    nixpkgs.url = "github:rivosinc/nixpkgs/rivos/nixos-22.11?allRefs=1";
  };

  outputs = {
    self,
    nixpkgs,
    ...
  }: let
    # Generate a user-friendly version number.
    gccVersion = nixpkgs.lib.fileContents ./VERSION;
    version = "${gccVersion}-${self.shortRev or "dirty"}";

    # System types to support.
    supportedSystems = [
      "x86_64-linux"
      "aarch64-linux"
      "riscv64-linux"
    ];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system:
      import nixpkgs {
        inherit system;
        overlays = [
          self.overlays.default
        ];
      });

    # From pkgs/top-level/stage.nix.
    # Non-GNU/Linux OSes are currently "impure" platforms, with their libc
    # outside of the store.  Thus, GCC, GFortran, & co. must always look for files
    # in standard system directories (/usr/include, etc.)
    noSysDirs = stdenv: (stdenv.buildPlatform.system
      != "x86_64-freebsd"
      && stdenv.buildPlatform.system != "i686-freebsd"
      && stdenv.buildPlatform.system != "x86_64-solaris"
      && stdenv.buildPlatform.system != "x86_64-kfreebsd-gnu");

    # Partially evaluate GCC with the flake.
    gccPkg = (import ./rivos/nix/git) {
      src = self;
      inherit version;
      gitRev = self.shortRev or null;
      dirty = !(self ? rev);
    };
    gccRivos = final:
      with final;
        lowPrio (wrapCCWith {
          cc = callPackage gccPkg {
            # TODO: libstdc++ fails to link on riscv with lld.
            # stdenv = rivosAdapters.withLinkFlags "-fuse-ld=bfd" gccStdenv;
            targetPackages.stdenv.cc.bintools = binutils;
            noSysDirs = noSysDirs stdenv;

            reproducibleBuild = true;
            profiledCompiler = false;

            libcCross =
              if stdenv.targetPlatform != stdenv.buildPlatform
              then libcCross
              else null;
            threadsCross =
              if stdenv.targetPlatform != stdenv.buildPlatform
              then threadsCross
              else {};

            isl =
              if !stdenv.isDarwin
              then fixedIsl final
              else null;
          };
          # This _will not work_ with llvm bintools. llvm-as is a very different tool than binutils' as.
          bintools = binutils;
        });

        # https://github.com/NixOS/nixpkgs/issues/21751
        fixedIsl = final: if !final.stdenv.buildPlatform.isx86 then final.isl_0_24.overrideAttrs (oldAttrs: {
          depsBuildBuild = [ final.buildPackages.stdenv.cc ];
          configureFlags = (oldAttrs.configureFlags or []) ++ [ "CC_FOR_BUILD=${final.buildPackages.stdenv.cc}/bin/${final.buildPackages.stdenv.cc.targetPrefix}cc" ];
        }) else final.isl_0_24;
  in {
    # Make gccRivos available as a separate package. Any use of it as a
    # compiler is opt-in.
    overlays.default = final: prev: {
      gccRivos = gccRivos final;
    };

    # Use this gcc for cross builds only.
    overlays.cross = final: prev: {
      gcc =
        if final.stdenv.targetPlatform != final.hostPlatform
        then gccRivos final
        else prev.gcc;
      gccCrossStageStatic = with final;
      assert stdenv.targetPlatform != stdenv.hostPlatform; let
        libcCross1 = binutilsNoLibc.libc;
      in
        wrapCCWith {
          cc = callPackage gccPkg {
            # TODO: libstdc++ fails to link on riscv with lld.
            # stdenv = rivosAdapters.withLinkFlags "-fuse-ld=bfd" gccStdenv;
            noSysDirs = noSysDirs stdenv;

            reproducibleBuild = true;
            profiledCompiler = false;

            isl =
              if !stdenv.isDarwin
              then fixedIsl final
              else null;

            # just for stage static
            crossStageStatic = true;
            langCC = false;
            libcCross = libcCross1;
            targetPackages.stdenv.cc.bintools = binutilsNoLibc;
            enableShared = false;
          };
          bintools = binutilsNoLibc;
          libc = libcCross1;
          extraPackages = [];
        };
    };

    # Force this to be the default gcc. Note: will cause a _lot_ of rebuilds!
    overlays.use_as_default_gcc = final: prev: {
      gcc = prev.gccRivos;
      gccFun = final.callPackage gccPkg;
    };

    # Provide some binary packages for selected system types.
    packages = forAllSystems (system: let
      gcc = (nixpkgsFor.${system}).gccRivos;
    in {
      inherit gcc;
      default = gcc;
    });

    formatter = forAllSystems (system: nixpkgsFor.${system}.alejandra);
  };
}
