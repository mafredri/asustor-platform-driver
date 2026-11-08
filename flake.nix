{
  description = "Install ASUSTOR kernel modules on NixOS";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      module =
        ({ stdenv, lib, kernel, ... }:

          stdenv.mkDerivation rec {
            name = "asustor-platform-driver-${version}-${kernel.version}";
            # Releases are git tags, which flakes cannot see; use the
            # commit, like scripts/version.sh does without a tag. Builds
            # without a lock file, as in CI, have no revision: "0-gdirty".
            version = "0-g${self.shortRev or self.dirtyShortRev or "dirty"}";

            src = ./.;

            hardeningDisable = [ "pic" "format" ];
            nativeBuildInputs = kernel.moduleBuildDependencies;

            buildFlags = [
              "KERNEL_MODULES=${kernel.dev}/lib/modules/${kernel.modDirVersion}"
            ];
            installFlags = [
              "KERNEL_MODULES=${placeholder "out"}/lib/modules/${kernel.modDirVersion}"
            ];

            preConfigure = ''
              sed -i '/depmod/d' Makefile
            '';

            meta = with lib; {
              description = "A kernel module to support ASUSTOR devices";
              homepage = "https://github.com/mafredri/asustor-platform-driver";
              license = licenses.gpl2Plus;
              maintainers = [ ];
              platforms = platforms.linux;
            };
          });
    in
    {
      nixosModules.default = ({ config, lib, ... }:
        let
          asustor-platform-driver = config.boot.kernelPackages.callPackage module { };
        in
        {
          options = {
            hardware.asustor.enable = lib.mkEnableOption "Enable the ASUSTOR kernel module";
          };
          config = lib.mkIf config.hardware.asustor.enable {
            boot.extraModulePackages = [ asustor-platform-driver ];
          };
        });
      # Builds against the default and the latest kernel in nixpkgs; CI
      # builds both.
      packages.${system} = {
        default = pkgs.linuxPackages.callPackage module { };
        latest = pkgs.linuxPackages_latest.callPackage module { };
      };
      formatter.${system} = pkgs.nixpkgs-fmt;
    };
}
