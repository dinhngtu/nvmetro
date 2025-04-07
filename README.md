NVMetro source code
===================

Repository structure
--------------------

| Directory        | Description                             |
| ---------------- | --------------------------------------- |
| patches-v5.10.66 | NVMetro kernel patch for Linux v5.10.66 |
| mdev-client      | UIF implementations                     |
| encryptor-sgx    | SGX enclave for encryptor UIF           |
| vmscripts        | Evaluation scripts and instructions     |
| eval             | Evaluation parser templates and scripts |

For evaluation instructions, please see the [vmscripts README](vmscripts/README.md).

References
----------

Extended journal paper: Virtual NVMe-based Storage Function Framework with Fast I/O Request State Management https://doi.org/10.1109/TC.2025.3558033
Original conference paper: Flexible NVMe Request Routing for Virtual Machines https://doi.org/10.1109/IPDPS57955.2024.00077
