#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cassert>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/ioctl.h>

#include "nvme_core.hpp"
#include "nvme_xcow.hpp"
#include "util.hpp"
#include "util/mdev.hpp"
#include "prp.hpp"
#include "vm.hpp"
#include "xcow/deref.hpp"
#include "xcow/xcowfmt.hpp"
#include "xcow/blk_iter.hpp"

using namespace xcow;

nvme_xcow::nvme_xcow(
    const std::shared_ptr<mapping> &vm,
    int nfd,
    int bfd,
    xcow::XcowFile *file,
    std::vector<bool> *clock,
    workqueue_type *wq)
    : nvme(vm, nfd), _bfd{{bfd}}, _file(file), _snap(_file->open_write()), _ring(8192, 0, std::span(_bfd), {}),
      _clock(clock), _wq(wq) {
    auto fsize = _snap.file().fsize();
    auto &idns = id_vns(1);
    idns->nsze = idns->ncap = fsize >> lba_shift(*idns);
    idns->nuse = 0;
    std::fill(std::begin(idns->nvmcap), std::end(idns->nvmcap), 0);
    idns->noiob = _snap.file().cluster_size() / (1 << lba_shift(*idns));
    // since we don't track NUSE, we can't report THINP
    idns->nsfeat |= (1 << 4); // optperf
    idns->npwg = idns->npwa = idns->nows = idns->noiob - 1;

    nvme_mdev_id_vns new_idns{};
    new_idns.nsid = 1;
    memcpy(&new_idns.data[0], idns.get(), std::size(new_idns.data));
    if (ioctl(nfd, NVME_MDEV_NOTIFYFD_SET_ID_VNS, &new_idns) < 0)
        throw std::system_error(errno, std::generic_category(), "cannot set ns1 id data");

    _clock->resize(fsize >> _snap.file().cluster_bits());
}

std::pair<nvme_xcow::cow_ticket_type *, io_uring_sqe *> nvme_xcow::do_cow(
    uint32_t tag,
    off_t inoff,
    off_t outoff,
    size_t nbytes) {
    auto ticket = new cow_ticket_type(tag, nbytes);
    ticket->count += 2;
    auto rqe = _ring.queue_read(ticket, ticket->mem.get(), nbytes, -1, true, 0, inoff);
    rqe->flags |= IOSQE_IO_LINK;
    auto wqe = _ring.queue_write(ticket, ticket->mem.get(), nbytes, -1, true, 0, outoff);
    return std::make_pair(ticket, wqe);
}

nm_outcome nvme_xcow::do_snapshot([[maybe_unused]] size_t sq, const nvme_command &cmd, [[maybe_unused]] uint32_t tag) {
    if (!id_vns(cmd.common.nsid))
        return nm_reply(tag, NVME_SC_DNR | NVME_SC_INVALID_NS);
    if (cmd.common.cdw2[0] || cmd.common.cdw2[1] || cmd.common.metadata || cmd.common.dptr.prp1 ||
        cmd.common.dptr.prp2 || cmd.common.cdw10 || cmd.common.cdw11 || cmd.common.cdw12 || cmd.common.cdw13 ||
        cmd.common.cdw14 || cmd.common.cdw15)
        return nm_reply(tag, NVME_SC_DNR | NVME_SC_INVALID_FIELD);
    _snap = _file->snap_create(_snap);
    return nm_reply(tag, NVME_SC_SUCCESS);
}

nm_outcome nvme_xcow::do_read([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    int lbas = ns_lba_shift(cmd.rw.nsid);
    auto clus_lba_shift = cluster_bits() - lbas;
    __u64 vblk = cmd.rw.slba >> clus_lba_shift;
    if (vblk != (cmd.rw.slba + cmd.rw.length) >> clus_lba_shift) {
        printf("multiblock command %zu %#x: %#llx+%#hx\n", sq, tag, cmd.rw.slba, cmd.rw.length);
        blk_iter bi(cmd.rw.slba, static_cast<size_t>(cmd.rw.length) + 1, clus_lba_shift);
        nvme_cmd_lba_iter lit(*this, cmd);
        auto ticket = new iovecs_ticket<xcow_ticket>(tag);
        ticket->iovecss.reserve(((cmd.rw.slba + cmd.rw.length) >> clus_lba_shift) - vblk + 1);
        // preadv2() doesn't support RWF_DSYNC so assume io_uring_prep_readv2() doesn't either
        if (cmd.rw.control & NVME_RW_FUA) {
            ticket->count++;
            auto sqe = _ring.queue_fsync(ticket, true, 0, IORING_FSYNC_DATASYNC);
            sqe->flags |= IOSQE_IO_LINK;
        }
        for (; !bi.at_end(); bi++) {
            auto tl = _snap.translate_read(*bi << lbas);
            auto off = *bi % (1 << clus_lba_shift);
            if (XlateBits::is_empty(tl)) {
                for (auto i = bi.size(); i > 0; i--) {
                    assert(!lit.at_end());
                    auto dt = *lit;
                    std::fill(dt.begin(), dt.end(), uint8_t(0));
                    lit++;
                }
            } else {
                auto &iovecs = ticket->iovecss.emplace_back();
                iovecs.reserve(cluster_size() / NVME_PAGE_SIZE + 1);
                for (auto i = bi.size(); i > 0; i--) {
                    assert(!lit.at_end());
                    iovec_append(iovecs, *lit);
                    lit++;
                }
                ticket->count++;
                _ring.queue_readv(ticket, iovecs, true, 0, XlateBits::decode_leaf(tl) + (off << lbas), 0);
            }
        }
        if (ticket->count > 0) {
            ticket->aux[0] = AUXCMD_KEEP;
            return ticket;
        } else {
            // all blocks are absent or already erased, no need to do anything
            delete ticket;
            return nm_reply(tag, NVME_SC_SUCCESS, UINT64_MAX, AUXCMD_KEEP);
        }
    } else {
        // do_alloc_one_tx will not commit its xlate entry before the allocation/cow is done
        // therefore it's safe to read from an entry snapshot even during a lock period
        auto tl = _snap.translate_read(cmd.rw.slba << lbas);
        if (XlateBits::is_empty(tl)) {
            for (nvme_cmd_page_iter pit(*this, cmd); !pit.at_end(); pit++) {
                auto dt = *pit;
                std::fill(dt.begin(), dt.end(), uint8_t(0));
            }
            return nm_reply(tag, NVME_SC_SUCCESS, 0, AUXBITS_VALID);
        } else {
            return nm_reply(tag, NVME_SC_SUCCESS, tl, AUXCMD_FORWARD);
        }
    }
}

/*
 * metadata and block allocation/cow operations need to be synchronized wrt each other
 * otherwise we'd have a race condition as follows (for 2 requests on the same cluster):
 * - request 1: translate_write | ---------------------------------------------> | fallocate -> set_aux -> nvme write
 * - request 2:                    | translate_write -> set_aux -> nvme write |
 * (bars are relative to each other in terms of ordering)
 * IOW: the fallocate() of request 1 might happen after the nvme write of request 2,
 * thus wiping out all changes made by request 2
 * our solution: make them transactional
 * - block [range] locking (for multi-block ops)
 * - request continuation mechanism when their block is locked by an alloc/cow
 * - delay metadata update until after alloc/cow completes
 */

nm_outcome nvme_xcow::do_write([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    int lbas = ns_lba_shift(cmd.rw.nsid);
    auto clus_lba_shift = cluster_bits() - lbas;
    auto vblk = cmd.rw.slba >> clus_lba_shift;
    if (vblk != (cmd.rw.slba + cmd.rw.length) >> clus_lba_shift) {
        printf("multiblock command %zu %#x: %#llx+%#hx\n", sq, tag, cmd.rw.slba, cmd.rw.length);
        blk_iter bi(cmd.rw.slba, static_cast<size_t>(cmd.rw.length) + 1, clus_lba_shift);
        nvme_cmd_lba_iter lit(*this, cmd);
        // ticket iovecs must be alive until submission
        auto ticket = new iovecs_ticket<xcow_ticket>(tag);
        ticket->iovecss.reserve(((cmd.rw.slba + cmd.rw.length) >> clus_lba_shift) - vblk + 1);
        for (; !bi.at_end(); bi++) {
            auto &iovecs = ticket->iovecss.emplace_back();
            iovecs.reserve(cluster_size() / NVME_PAGE_SIZE + 1); // ensure that a single cluster write fits
            for (auto i = bi.size(); i > 0; i--) {
                assert(!lit.at_end());
                iovec_append(iovecs, *lit);
                lit++;
            }

            auto entry = _snap.tx_write_prep(*bi << lbas);
            vblk = *bi >> clus_lba_shift;
            auto off = *bi % (1 << clus_lba_shift);

            if (XlateBits::needs_alloc(*entry) && !(*_clock)[vblk]) {
                do_alloc_one_tx(noop_tag, vblk, entry);
                (*_clock)[vblk] = true;
            }
            auto flags = (cmd.rw.control & NVME_RW_FUA) ? RWF_DSYNC : 0;

            // do_write_one does not increment the ticket
            // plus the work might be queued so we have to do it now
            ticket->count++;
            if ((*_clock)[vblk])
                _wq->emplace(vblk, [=, this, &iovecs](__s32 us) -> nm_outcome {
                    // TODO: we leak the allocated blocks on cows
                    if (us < 0) {
                        if (!--ticket->count)
                            delete ticket;
                        // flush?
                        return nm_reply(tag, translate_uring_status(us));
                    } else {
                        return do_write_one(ticket, iovecs, entry, off, flags);
                    }
                });
            else
                do_write_one(ticket, iovecs, entry, off, flags);
        }
        return ticket;
    } else {
        auto entry = _snap.tx_write_prep(cmd.rw.slba << lbas);
        if (XlateBits::needs_alloc(*entry) && !(*_clock)[vblk]) {
            auto [ticket, inoff, outoff] = do_alloc_one_tx(tag, vblk, entry);
            (*_clock)[vblk] = true;
            set_aux(ticket->aux, outoff, AUXBITS_VALID | AUXBITS_WRITABLE | AUXCMD_FORWARD);
            return ticket;
        } else if ((*_clock)[vblk]) {
            _wq->emplace(vblk, [=](__s32 us) -> nm_outcome {
                return nm_reply(tag, translate_uring_status(us), *entry, AUXCMD_FORWARD);
            });
            return std::monostate{};
        } else {
            return nm_reply(tag, NVME_SC_SUCCESS, *entry, AUXCMD_FORWARD);
        }
    }
}

std::tuple<xcow_ticket *, uint64_t, uint64_t> nvme_xcow::do_alloc_one_tx(
    uint32_t tag,
    uint64_t vblk,
    XlateLeaf *entry) {
    xcow_ticket *ticket;
    uint64_t inoff, outoff;
    size_t tmp;
    assert(XlateBits::needs_alloc(*entry));
    std::tie(outoff, tmp) = _snap.alloc_data_cluster();
    if (XlateBits::is_empty(*entry)) {
        inoff = UINT64_MAX;
        ticket = new xcow_ticket(tag);
        ticket->count++;
        _ring.queue_fallocate(ticket, true, 0, FALLOC_FL_ZERO_RANGE, outoff, cluster_size());
    } else { // XlateBits::needs_cow(*entry)
        inoff = XlateBits::decode_leaf(*entry);
        io_uring_sqe *cow_wqe;
        std::tie(ticket, cow_wqe) = do_cow(tag, inoff, outoff, cluster_size());
    }
    ticket->locked_cluster = vblk;
    ticket->last =
        cleanup([&, entry, outoff] { _snap.tx_write_commit(*entry, XlateBits::encode_leaf(outoff, true), nullptr); });
    return std::make_tuple(ticket, inoff, outoff);
}

nm_outcome nvme_xcow::do_write_one(
    xcow_ticket *ticket,
    const std::vector<iovec> &iovecs,
    XlateLeaf *entry,
    uint64_t off,
    int flags) {
    auto location = XlateBits::decode_leaf(*entry) + off;
    _ring.queue_writev(ticket, iovecs, true, 0, location, flags);
    // we must return the ticket once again to kick the uring sq
    return ticket;
}

nm_outcome nvme_xcow::do_write_zeroes([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    int lbas = ns_lba_shift(cmd.rw.nsid);
    auto clus_lba_shift = cluster_bits() - lbas;
    __u64 vblk = cmd.rw.slba >> clus_lba_shift;
    if (vblk != (cmd.rw.slba + cmd.rw.length) >> clus_lba_shift) {
        blk_iter bi(cmd.rw.slba, static_cast<size_t>(cmd.rw.length) + 1, clus_lba_shift);
        auto ticket = new xcow_ticket(tag);
        for (; !bi.at_end(); bi++) {
            auto old = _snap.translate_read(*bi << lbas);
            auto off = *bi % (1 << clus_lba_shift);
            if (XlateBits::is_empty(old)) {
                continue;
            } else if (bi.size() == (1 << clus_lba_shift)) {
                _snap.erase(*bi << lbas);
                continue;
            } else if (old & XlateBits::writable) {
                ticket->count++;
                _ring.queue_fallocate(
                    ticket,
                    true,
                    0,
                    FALLOC_FL_ZERO_RANGE,
                    XlateBits::decode_leaf(old) + (off << lbas),
                    bi.size() << lbas);
            } else {
                XlateLeaf prev{};
                auto tl = _snap.translate_write(*bi << lbas, &prev);
                loff_t inoff = XlateBits::decode_leaf(prev), outoff = XlateBits::decode_leaf(tl);
                auto [cow_ticket, cow_wqe] = do_cow(noop_tag, inoff, outoff, cluster_size());
                cow_wqe->flags |= IOSQE_IO_LINK;
                ticket->count++;
                _ring.queue_fallocate(ticket, true, 0, FALLOC_FL_ZERO_RANGE, outoff + (off << lbas), bi.size() << lbas);
            }
        }
        if (ticket->count > 0) {
            return ticket;
        } else {
            // all blocks are absent or already erased, no need to do anything
            delete ticket;
            return nm_reply(tag, NVME_SC_SUCCESS);
        }
    } else {
        auto old = _snap.translate_read(cmd.rw.slba << lbas);
        auto nblocks = static_cast<size_t>(cmd.rw.length) + 1;
        if (XlateBits::is_empty(old)) {
            // already zero, no need to write anything
            return nm_reply(tag, NVME_SC_SUCCESS);
        } else if (nblocks == (1 << clus_lba_shift)) {
            _snap.erase(cmd.rw.slba << lbas);
            return nm_reply(tag, NVME_SC_SUCCESS);
        } else if (!(old & XlateBits::writable)) {
            XlateLeaf prev{};
            auto tl = _snap.translate_write(cmd.rw.slba << lbas, &prev);
            loff_t inoff = XlateBits::decode_leaf(prev), outoff = XlateBits::decode_leaf(tl);
            auto [cow_ticket, cow_wqe] = do_cow(noop_tag, inoff, outoff, cluster_size());
            set_aux(cow_ticket->aux, tl, AUXCMD_FORWARD);
            return cow_ticket;
        } else {
            return nm_reply(tag, NVME_SC_SUCCESS, old, AUXCMD_FORWARD);
        }
    }
}

nm_outcome nvme_xcow::do_flush([[maybe_unused]] size_t sq, [[maybe_unused]] const nvme_command &cmd, uint32_t tag) {
    auto ticket = new xcow_ticket(tag);
    ticket->count++;
    _ring.queue_fsync(ticket, true, 0, IORING_FSYNC_DATASYNC);
    ticket->aux[0] = AUXCMD_KEEP | AUXCMD_FORWARD;
    return ticket;
}

nm_outcome nvme_xcow::submit_async(size_t sq, const nvme_command &cmd, uint32_t tag) {
    try {
        if (sq == 0) {
            return nm_reply(tag, NVME_SC_DNR | NVME_SC_INVALID_OPCODE);
        } else if (cmd.common.opcode == nvme_cmd_read) {
            return do_read(sq, cmd, tag);
        } else if (cmd.common.opcode == nvme_cmd_write) {
            return do_write(sq, cmd, tag);
            //} else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
            // return do_write_zeroes(sq, cmd, tag);
        } else if (cmd.common.opcode == nvme_cmd_flush) {
            return do_flush(sq, cmd, tag);
        } else if (cmd.common.opcode == 0x81) {
            return do_snapshot(sq, cmd, tag);
        } else {
            DBG_PRINTF("sq %zu unknown opcode %#hhx cid %hu\n", sq, cmd.common.opcode, cmd.common.command_id);
        }
        return nm_reply(tag, NVME_SC_DNR | NVME_SC_INVALID_OPCODE);
    } catch (const hwm_exception &) {
        return nm_reply(tag, NVME_SC_DNR | NVME_SC_CAP_EXCEEDED);
    } catch (const disk_hwm_exception &) {
        return nm_reply(tag, NVME_SC_DNR | NVME_SC_CAP_EXCEEDED);
    }
}

cq_window nvme_xcow::get_pending_completions(std::span<io_uring_cqe *> cqebuf) {
    return _ring.cq_get_ready(cqebuf);
}
