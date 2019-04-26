# OP-TEE resource description
The optee_rd.dts is the first attempt at proving and prototyping generalisation
of support for Trusted OSs in TF-A using OP-TEE as an example. Currently, OP-TEE
has a [dispatcher] component in TF-A which manages OP-TEE initialisation and its
communication with the Normal world.

The Arm Secure Partition Client Interface specification ([SPCI]) describes a
framework where communication between clients in normal world e.g. OS and a
secure partition (SP) e.g. OP-TEE is enabled by the Secure Partition Manager
(SPM) component instead of a SP specific dispatcher.

The SPM uses a description of resources (RD) required by each SP to operate
correctly to ensure that the SP gets what it needs. This is done at boot
time. With secure virtualisation, the RD is even more important as this enables
the SPM to create stage 2 translation tables on behalf of the SP. This is key
for guaranteeing address space isolation.

In the absence of secure virtualisation (on Armv8.3 and less), there is no
translation table management done by the SPM for S-EL1 SPs. The RD enables a
migration path for a Trusted OS to work with the SPM. It also enables the SPM to
describe the RX/TX buffers allocated to the SP. These buffers are key to
enabling communication with a SP.

The optee_rd.dts takes the first step in this direction by describing a list of
address space regions (memory and device) that OP-TEE must map in its stage 1
translation tables. With secure virtualisation, the SPM will create the stage 2
translations for these regions in the core and applicable SMMU instance. The SPM
converts the RD to a standard message that it passed to OP-TEE prior to its
initialisation. The RX/TX buffer description is also included in this message.

The RD is included in the TF-A build as the Trusted OS firmware configuration
(env variable FVP_TOS_FW_CONFIG & option --tos-fw-config in the [fiptool]) after
following these steps. The intent is to convert optee_rd.dts into a dts that can
be consumed by TF-A as FVP_TOS_FW_CONFIG.

```
cat optee_rd.dts | cpp -P -x c -I ./ -I ../../../../lib/libutils/ext/include/ - | sed -n '/\/dts-v1\//,$p' > optee_rd.dts.pre
```
```
dtc -I dts -O dtb optee_rd.dts.pre > optee_rd.dtb
```
```
dtc -I dtb -O dts optee_rd.dtb > "path and filename of tf-a suitable dts"
```

This work builds on top of the SPCI Alpha 2 specification. The format of the RD
is fairly basic and will evolve as the prototyping activity progresses. The
conversion steps listed above should either disappear or be simplified once the
RD support in SPM evolves.

[dispatcher]: https://github.com/ARM-software/arm-trusted-firmware/tree/master/services/spd/opteed
[SPCI]: https://connect.arm.com/dropzone/systemarch/DEN0077A%5FSecure%5FPartition%5FInterface%5FSpecification%5F1.0%5FAlpha%5F2.pdf
[fiptool]: https://github.com/ARM-software/arm-trusted-firmware/blob/master/tools/fiptool/fiptool.c