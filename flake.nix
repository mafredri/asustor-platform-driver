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
            version = "0.1";

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
              license = licenses.gpl3;
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
      formatter.${system} = pkgs.nixpkgs-fmt;
    };
}
