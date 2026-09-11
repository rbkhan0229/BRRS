:orphan:

Current BRRS development (2026-09-09):

    Use branch vehicle-experiments in this repository. It contains the latest
    vehicle firmware, Stage0-Exp5 campaign tools, and sequential Exp2/Exp5
    measurements of physical links N2-N7. Earlier source checkpoints are
    preserved as Git tags; no additional full SDK copy is needed.

    Start with docs/BRRS_WORKSPACE_KR.md, docs/BRRS_DATA_CATALOG_KR.md, and
    Drivers/API/BRRS_CIR_LINKS_KR.md. The exact Stage0-Exp5 case matrices and
    self-contained field procedures are in docs/experiments/BRRS_EXPERIMENT_FULL_KR.md,
    BRRS_EXPERIMENT_STANDARD_KR.md, BRRS_EXPERIMENT_ESSENTIAL_KR.md, and
    BRRS_EXPERIMENT_LITE_KR.md. Historical experiment scripts under
    experiments/legacy are provenance, not the current execution entry point.

    Offline checks (no SSH, flash, or RF):
      python3 -B -m unittest discover -s Drivers/API/tests -p 'test_*.py' -v
      python3 tools/verify_experiment_archive.py

    Large local logs and firmware images remain outside Git. Their paths and
    hashes are indexed in docs/reproducibility; this is not a cloud backup of
    those artifacts. See docs/BRRS_WORKSPACE_KR.md before removing any old copy.

Local BRRS workspace policy:

    The repository contains firmware sources, SES project definitions, analysis
    scripts, and experiment documentation. Generated logs, processed results,
    reports, and transfer archives are stored beside this repository under the
    parent DWM3000 directory. SES Output directories are rebuildable and are not
    archived or version-controlled. The machine-local Nordic nRF5 SDK remains at
    Drivers/API/Build_Platforms/nRF52840-DK/sdk.

Release folder structure:

    ├─── README.md                                                              <--- Current file - it describes the folder structure
    ├─── Drivers                                                                <--- Drivers APIs to interact directly with the UWB transceiver
    │    ├── API
    │    │   ├── Build_Platforms                                                <--- Projects using the drivers for a specific hardware platform
    │    │   │   └── ...
    │    │   ├── Shared                                                         <--- Drivers source code
    │    │   │   └── ...
    │    │   └── Src                                                            <--- Drivers simple examples (including ranging)
    │    │       └── ...
    │    ├── LICENSES
    │    │   └── ...
    │    ├── Changelog.md
    │    ├── QM33XXX_DW3XXX_Software_API_Guide-<x>p<y>.pdf
    │    └── README.md
    └─── SDK                                                                    <--- SDK main directory
         ├─── Binaries                                                          <--- Prebuilt binary files ready to be flashed
         │    ├── <target_1>
         │    │    ├── <target_1>-DW3_QM33_SDK_CLI-<OS>.hex
         │    │    └── <target_1>-DW3_QM33_SDK_UCI-<OS>.hex
         │    └── <target_2>
         │         └── ...
         ├─── Documentation                                                     <--- All the documentation for this SDK
         │          ├─── Quick Start Guide                                      <--- Quick Start Guides for each target
         │          │    ├── <target_1>_Quick_Start_Guide_QM33SDK-<x.x.x>.pdf
         │          │    ├── <target_2>_Quick_Start_Guide_QM33SDK-<x.x.x>.pdf
         │          │    └── ...
         │          ├─── Developer Manual                                       <--- Developer Manuals for each target
         │          │    ├── <target_1>_Developer_Manual_QM33SDK-<x.x.x>.pdf
         │          │    ├── <target_2>_Developer_Manual_QM33SDK-<x.x.x>.pdf
         │          │    └── ...
         │          ├─── Drivers                                                <--- Drivers APIs documentation to interact directly with the UWB transceiver
         │          │    └── QM33XXX_DW3XXX_Software_API_Guide-<x>p<y>.pdf
         │          └─── uwb-stack                                              <--- UWB stack documentation
         │               ├── uwb-fira-protocol-R<x.x.x-y>.pdf
         │               ├── uwb-l1-api-R<x.x.x-y>.pdf
         │               ├── uwb-l1-configuration-R<x.x.x-y>.pdf
         │               ├── uwb-qhal-api-R<x.x.x-y>.pdf
         │               ├── uwb-qosal-api-R<x.x.x-y>.pdf
         │               ├── uwb-qplatform-api-R<x.x.x-y>.pdf
         │               ├── uwb-uci-messages-api-R<x.x.x-y>.pdf
         │               └── uwb-uwbmac-api-R<x.x.x-y>.pdf
         ├─── Firmware                                                          <--- SDK source code files and CMake projects
         │    ├── README.md
         │    └── DW3_QM33_SDK_<x.x.x>.zip
         ├─── Tools                                                             <--- Additional tools to interact with Qorvo's UWB devices
         │    ├── GUI                                                           <--- Qorvo One GUI to demonstrate two-way ranging capabilities
         │    │    ├── linux
         │    │    │   └── QorvoOneTWR-<x.x.x>-x86_64-install.sh
         │    │    ├── macOS
         │    │    │   └── QorvoOneTWR-<x.x.x>.dmg
         │    │    └── Windows
         │    │        └── QorvoOneTWR-<x.x.x>-setup.exe
         │    └── uwb-qorvo-tools                                               <--- Python scripts for more complex use cases
         │        └── ...
         └─── Release_Notes.pdf
