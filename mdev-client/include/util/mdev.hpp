#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

int mdev_open(const char *arg_iommu_group, const char *arg_mdev_uuid, std::vector<int> &sqfd);
void *mdev_vm_mmap(int mfd, size_t below_4g_mem_size, size_t &pvm_size);

struct hmb_dl_entry;
void *mdev_vm_mmap_hmb(
    int mfd,
    size_t below_4g_mem_size,
    uint32_t hmb_pages,
    std::span<volatile hmb_dl_entry> hmdl,
    uint32_t &mapped);
