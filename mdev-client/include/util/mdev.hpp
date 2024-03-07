#pragma once

#include <vector>
#include <poll.h>

int mdev_open(const char *arg_iommu_group, const char *arg_mdev_uuid, std::vector<int> &sqfd);
void *mdev_vm_mmap(int mfd, size_t below_4g_mem_size, size_t &pvm_size);
