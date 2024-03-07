- [Introduction](#introduction)
  - [Hardware](#hardware)
  - [Software environment](#software-environment)
- [Installation](#installation)
  - [Dependencies](#dependencies)
  - [Extracting and building the tools](#extracting-and-building-the-tools)
  - [Environment configuration](#environment-configuration)
  - [Setting up the remote NVMe target (for disk replication evals)](#setting-up-the-remote-nvme-target-for-disk-replication-evals)
- [Running the evaluations](#running-the-evaluations)
  - [Starting the VM-under-test (VMUT)](#starting-the-vm-under-test-vmut)
  - [Running the evaluation](#running-the-evaluation)
  - [Gathering and presenting results](#gathering-and-presenting-results)
- [Appendix](#appendix)
  - [Building the NVMetro kernel](#building-the-nvmetro-kernel)

Introduction
========

Hardware
--------

NVMetro was evaluated on the following hardware platforms:

For basic and disk replication evaluations:
- 2x Dell PowerEdge R420, each with 2x Intel Xeon E5-2420 v2, 48 GB RAM, Samsung SSD 970 EVO Plus 1TB
- Networking: Mellanox ConnectX Infiniband 4x QDR (40 Gbps) PCIe 2.0 x8
- CPU profile set to Performance

For disk encryption evaluations:
- Dell Precision 7540, Intel Core i5-9400H, 16 GB of RAM (2x8GB), Samsung SSD 970 EVO Plus 1TB
- SGX is enabled in BIOS with 128 MB EPC

Each machine must have a dedicated NVMe disk (of the same make and model, and preferably the same model shown above).
Virtualization and IOMMU must be enabled on all machines.
The procedure has only been tested on Intel machines, AMD ones are untested.

The installation of NVMetro must be carried out on **one single server** as well as the laptop to run SGX evaluations.
The other server serves as the NVMeoF target for disk replication evaluations, and necessitates a different configuration process.

Software environment
--------------------

| Software         | Version                    |
| ---------------- | -------------------------- |
| Operating system | Ubuntu Server 20.04 (UEFI) |
| Linux kernel     | 5.10.66 NVMetro            |
| QEMU             | 6.0.0                      |
| SPDK             | 21.10                      |
| Liburing         | 2.1                        |
| Intel SGX SDK    | 2.15                       |

Installation
============

To avoid unexpected issues, it is required to run all of the steps detailed below **as root**, inside the root home directory (`/root`).
For clarity and to avoid breaking lines when copying commands, each line is prefixed with `#` in the instructions, please be sure to remove them before running the command.

Download the NVMetro AE package and extract them directly inside `/root`, following this structure:

    /root/
        nvmetro-ae/
            apt.txt
            install.sh
            ...
        nvmetro.qcow2

Dependencies
------------

Install a clean Ubuntu Server environment and the latest package upgrades.

To save time and effort, the bundled script `nvmetro-ae/install.sh` is capable of detecting and installing the required dependencies, including the NVMetro kernel:

    # cd nvmetro-ae
    # ./install.sh

**Reboot after installation** to use the new kernel and configurations.

Extracting and building the tools
---------------------------------

    # cd /root
    # tar -xf nvmetro-ae/spdk.tar.gz
    # tar -xf nvmetro-ae/vmscripts.tar.gz
    # tar -xf nvmetro-ae/encryptor-sgx.tar.gz
    # make -C encryptor-sgx -j8
    # tar -xf nvmetro-ae/mdev-client.tar.gz
    # make -C mdev-client -j8

Environment configuration
-------------------------

Customize the evaluation configuration by editing the file `vmscripts/vars.sh`.
Lines with `CHANGE THIS` must be changed to fit your hardware.

    memsize=6G  # VM memory size
    nvmerepl=/dev/nvme1n1  # <-- CHANGE THIS to the path of the remote NVMeoF
            # disk as it appears on the local evaluation machine (for disk
            # replication evals)
    mdev_client=../mdev-client
    mcinfo=/dev/null
    memfile=/mnt/huge0/mdev.mem
    vmdisk=../nvmetro.qcow2
    spdk=../spdk
    laptop_hostname=nvme-sgx  # <-- CHANGE THIS for laptop-based evaluations

    target_ip=192.168.0.3  # <-- CHANGE THIS
    target_localpath=/dev/nvme0n1  # <-- CHANGE THIS

    if [ "$(hostname)" = "$laptop_hostname" ]  # NVMetro configuration on laptop (for SGX encryption evals)
    then
    guest_mac=ce:76:f8:2e:f1:6c
    cpus=4  # Number of vCPUs
    hcpus=2,3,6,7  # <-- CHANGE THIS to set the affinity of VM's vCPUs
    mthreads=2  # <-- Number of UIF threads
    sgx_mthreads=1  # <-- Number of SGX UIF threads
    mcpus=0,1,4,5  # <-- CHANGE THIS: UIF affinity (see numactl -C). I chose 4 logical CPUs corresponding to 2 physical cores here.
    spdkcpus=0x3  # <-- CHANGE THIS: SPDK affinity (see https://spdk.io/doc/app_overview.html)
    nvmeblk=/dev/nvme1n1  # CHANGE THIS: path to the NVMe disk for evaluations
    nvme=0000:6f:00.0  # CHANGE THIS: PCI address of the NVMe disk (see `lspci -D`)

    else  # NVMetro configuration on servers
    guest_mac=ce:76:f8:2e:f1:6b
    cpus=8  # Number of vCPUs
    hcpus=4,6,8,10,16,18,20,22  # <-- CHANGE THIS to set the affinity of VM's vCPUs
    mthreads=2  # <-- Number of UIF threads
    mcpus=0,2,12,14  # <-- CHANGE THIS: UIF affinity (see numactl -C)
    spdkcpus=0x5  # <-- CHANGE THIS: SPDK affinity (see https://spdk.io/doc/app_overview.html)
    nvmeblk=/dev/nvme0n1  # CHANGE THIS: path to the NVMe disk for evaluations
    nvme=0000:41:00.0  # CHANGE THIS: PCI address of the NVMe disk (see `lspci -D`)
    fi

Setting up the remote NVMe target (for disk replication evals)
--------------------------------------------------------------

The NVMetro replication evaluation requires two servers connected with Infiniband.
Set both adapters to **connected** mode with a MTU of **65520**.
When using Netplan, this can be done by putting the following script in `/etc/networkd-dispatcher/configuring.d/50-infiniband.sh` (remember to `chmod +x`):

    #!/bin/sh
    if [ "$IFACE" = "ibp8s0" ]  # replace with your Infiniband interface name
    then
        echo connected > /sys/class/net/$IFACE/mode
        echo 65520 > /sys/class/net/$IFACE/mtu
    fi

**NVMeoF server**

On a **dedicated** machine (not running NVMetro evaluations), set up the NVMeoF target (i.e. server):

    # cd nvmetro-ae
    # ./install-nvmeof.sh

Run the following command, replacing `a.b.c.d` with the IP of the target's Infiniband interface; and `/dev/nvmeXnX` with the local NVMe disk of the target:

    # jq ".ports[0].addr.traddr=\"a.b.c.d\" | .subsystems[0].namespaces[0].device.path=\"/dev/nvmeXnX\"" config.json > /etc/nvmet/config.json

Apply the NVMeoF configuration:

    # cd /root/nvmetcli
    # ./nvmetcli restore

Finally, to allow automatic formatting of the remote disk, **you must allow SSH-ing to the `root` account of this server without a password from the NVMetro evaluation server**.

**NVMeoF client**

On the computer that's running the NVMetro evaluations, put the following line in `/etc/nvme/discovery.conf`:

    --transport=rdma --traddr=a.b.c.d --trsvcid=4420 --nr-poll-queues=1

Replace `a.b.c.d` with the IP of the NVMeoF target (server) used above.

Run the following command to test the connection:

    # nvme connect-all

The remote disk should then appear. Note the path of the disk on the client (to be set as the `nvmerepl` configuration above). To disconnect from the target:

    # nvme disconnect-all

Running the evaluations
=======================

The evaluation of NVMetro includes 3 main steps:

1. Start the VM-under-test
2. Run evaluations
3. Gather and present results

Starting the VM-under-test (VMUT)
---------------------------------

First, on each evaluation server, create a bridge for the VMUT. The bridge will be called `br0`:

    # cd vmscripts
    # ./create-bridge.sh eno1

Replace `eno1` with the interface name of the host Ethernet adapter.

The evaluation package includes a script to start the VMUT, refer to the following table for the correct command:

| Configuration | Figures  | Script                          |
| ------------- | -------- | ------------------------------- |
| NVMetro       | 3, 4, 9  | `./run-qemu-bpf-passthrough.sh` |
| MDev          | 3, 4, 9  | `./run-qemu-nobpf.sh`           |
| Passthrough   | 3, 4, 9  | `./run-qemu-passthrough.sh`     |
| QEMU          | 3, 4, 9  | `./run-qemu-qvblk.sh`           |
| Vhost         | 3, 4, 9  | `./run-qemu-scsi.sh`            |
| SPDK          | 3, 4, 9  | `./run-qemu-spdk.sh`            |
| NVMetro Encr. | 5, 6, 10 | `./run-qemu-encryptor.sh`       |
| NVMetro SGX   | 5, 6, 10 | `./run-qemu-encryptor-sgx.sh`   |
| `dm-crypt`    | 5, 6, 10 | `./run-qemu-encryptor-scsi.sh`  |
| NVMetro Repl. | 7, 8, 11 | `./run-qemu-replicator.sh`      |
| `dm-mirror`   | 7, 8, 11 | `./run-qemu-mirror-scsi.sh`     |

The evaluation script sets the VMUT's MAC to `ce:76:f8:2e:f1:6c` on the laptop (for SGX evals) and `ce:76:f8:2e:f1:6b` on the server.
You should reserve an IP address for each MAC listed above to ensure that the VMUT has a predictable IP for evaluation purposes.

Finally, test the connection to the VMUT with SSH. The default credential of the VMUT is `root/123456`.

**You must make sure that you can SSH to the VMUT from the host without a password.**

Running the evaluation
----------------------

**Note:** Before running the replication evaluations, you must configure NVMeoF as described above.

Evaluation of NVMetro requires PowerShell, which should be installed by our preparation script.
Start PowerShell from the evaluation script directory:

    # cd vmscripts
    # pwsh

The evaluation script is called `doall.ps1`.
Since it makes use of SSH to access the VM, you must specify the `-TargetHost` to that of the VMUT's IP.
Refer to the following table for the arguments:

| Configuration | Figures  | `TargetDevice` | `FormatMode`  | `BenchPrefix` example |
| ------------- | -------- | -------------- | ------------- | --------------------- |
| NVMetro       | 3, 4, 9  | `/dev/nvme0n1` | `Mdev`        | `server-bpfpt`        |
| MDev          | 3, 4, 9  | `/dev/nvme0n1` | `Mdev`        | `server-nobpf`        |
| Passthrough   | 3, 4, 9  | `/dev/nvme0n1` | `Passthrough` | `server-passthrough`  |
| QEMU          | 3, 4, 9  | `/dev/vdb`     | `Mdev`        | `server-qvblk`        |
| Vhost         | 3, 4, 9  | `/dev/sda`     | `Mdev`        | `server-scsi`         |
| SPDK          | 3, 4, 9  | `/dev/vdb`     | `Spdk`        | `server-spdk`         |
| NVMetro Encr. | 5, 6, 10 | `/dev/nvme0n1` | `Mdev`        | `server-enc`          |
| NVMetro SGX   | 5, 6, 10 | `/dev/nvme0n1` | `Mdev`        | `laptop-encsgx`       |
| `dm-crypt`    | 5, 6, 10 | `/dev/sda`     | `Mdev`        | `server-encscsi`      |
| NVMetro Repl. | 7, 8, 11 | `/dev/nvme0n1` | `Repl`        | `server-replicator`   |
| `dm-mirror`   | 7, 8, 11 | `/dev/nvme0n1` | `Repl`        | `server-mirrorscsi`   |

The `BenchPrefix` argument is the name of the experiment, and is listed **as an example**.
You can rename the argument as necessary, but it should be in the form `hostname-experimentname`, with only one dash and without special characters in each word (i.e. only `[a-zA-Z0-9]+`).
This helps the result parser recognize the results.

Gathering and presenting results
--------------------------------

Most results will be stored directly on the host in a directory called `nmbpf`.
Parts of the results are stored on the VMUT, copy it to the host:

    # scp -r a.b.c.d:nmbpf/ .

Replace `a.b.c.d` with the VMUT's IP address.

The NVMetro results require PowerShell to parse. Use the following commands:

    # cd /root/nvmetro-ae/eval
    # pwsh
    > Import-Module ../ReverseTemplate
    > ./parse.ps1

The parser script will produce a list of `.csv` files containing the compiled experiment results.
The formats of these results are detailed below:

**Fio performance**: `fio.csv`

| Column name | Meaning                                  |
| ----------- | ---------------------------------------- |
| `host`      | Hostname (as specified in `BenchPrefix`) |
| `vmmode`    | Configuration (in `BenchPrefix`)         |
| `runno`     | Run attempt #                            |
| `numjobs`   | Number of parallel jobs                  |
| `bs`        | Fio blocksize                            |
| `runtype`   | Operation (read, write, read/write mix)  |
| `iodepth`   | Queue depth                              |
| `read_ios`  | Number of reads performed in 10 seconds  |
| `write_ios` | Number of writes performed in 10 seconds |

**YCSB performance**: `ycsb.csv`

| Column name | Meaning                                         |
| ----------- | ----------------------------------------------- |
| `host`      | Hostname (as specified in `BenchPrefix`)        |
| `vmmode`    | Configuration (in `BenchPrefix`)                |
| `runno`     | Run attempt #                                   |
| `nrjobs`    | Number of parallel jobs                         |
| `runtime`   | Total YCSB runtime                              |
| `tput`      | YCSB throughput                                 |
| `workload`  | Workload type (A-F)                             |
| `step`      | YCSB step (load/run)                            |
| `jobno`     | Number of individual job (out of `nrjobs` jobs) |

**Kernbench performance**: `kernbench.csv`

| Column name | Meaning                                  |
| ----------- | ---------------------------------------- |
| `host`      | Hostname (as specified in `BenchPrefix`) |
| `vmmode`    | Configuration (in `BenchPrefix`)         |
| `runno`     | Run attempt #                            |
| `step`      | Step (extract/compile)                   |
| `system`    | System time, as reported by `time(1)`    |
| `user`      | User time, as reported by `time(1)`      |
| `elapsed`   | Elapsed time, as reported by `time(1)`   |

**CPU usage**: `cpu-*.csv`

The CPU time columns (`user`, `nice`, etc.) follow the ones defined in `/proc/stat` (see `proc(5)`).
Other columns follow the definitions listed in the above sections.

| Column name  | Meaning                                                                    |
| ------------ | -------------------------------------------------------------------------- |
| `host`       | Hostname (as specified in `BenchPrefix`)                                   |
| `vmmode`     | Configuration (in `BenchPrefix`)                                           |
| `runno`      | Run attempt #                                                              |
| `user`       | Time spent in user mode (`/proc/stat`)                                     |
| `nice`       | Time spent in user mode with low priority                                  |
| `system`     | Time spent in system mode                                                  |
| `idle`       | Time spent in the idle task                                                |
| `iowait`     | Time waiting for I/O to complete                                           |
| `irq`        | Time servicing interrupts                                                  |
| `softirq`    | Time servicing softirqs                                                    |
| `steal`      | Stolen time                                                                |
| `guest`      | Time spent running a guest virtual CPU                                     |
| `guest_nice` | Time spent running a niced guest                                           |
| `cpustep`    | If measurement made before experiment (`pre`) or after experiment (`post`) |

Total CPU time per configuration ($Total$) reported in our paper is calculated as:

$Total_{user} = \frac{\sum_{POST}{user} - \sum_{PRE}{user}}{Count(user) \times \frac{1}{2} \times 100}$

$Total_{system} = \frac{\sum_{POST}{system} - \sum_{PRE}{system}}{Count(system) \times \frac{1}{2} \times 100}$

$Total = Total_{user} + Total_{system}$

($Count(*) \times \frac{1}{2}$ is needed since $Count(*)$ counts both pre- and post-times)

Appendix
========

Building the NVMetro kernel
---------------------------

The kernel source code is included with the evaluation package.

    # cd /root
    # tar -xf nvmetro-ae/linux-mdev-nvme.tar.gz

Use the existing configuration available in the AE package to install the kernel:

    # cp nvmetro-ae/linux-config linux-mdev-nvme/.config
    # cd linux-mdev-nvme
    # make headers_install
    # make all -j8
    # make -C tools bpf -j8
    # make M=samples/bpf -j8
    # make modules_install
    # make install
    # reboot
