/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMBUS_CHANVAR_H_
#define _VMBUS_CHANVAR_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/sysctl.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/include/vmbus.h>

typedef struct {
	/*
	 * offset in bytes from the start of ring data below
	 */
	volatile uint32_t       write_index;
	/*
	 * offset in bytes from the start of ring data below
	 */
	volatile uint32_t       read_index;
	/*
	 * NOTE: The interrupt_mask field is used only for channels, but
	 * vmbus connection also uses this data structure
	 */
	volatile uint32_t       interrupt_mask;
	/* pad it to PAGE_SIZE so that data starts on a page */
	uint8_t                 reserved[4084];

	/*
	 * WARNING: Ring data starts here
	 *  !!! DO NOT place any fields below this !!!
	 */
	uint8_t			buffer[0];	/* doubles as interrupt mask */
} __packed hv_vmbus_ring_buffer;

typedef struct {
	hv_vmbus_ring_buffer*	ring_buffer;
	struct mtx		ring_lock;
	uint32_t		ring_data_size;	/* ring_size */
} hv_vmbus_ring_buffer_info;

struct vmbus_channel {
	/*
	 * NOTE:
	 * Fields before ch_txbr are only accessed on this channel's
	 * target CPU.
	 */
	uint32_t			ch_flags;	/* VMBUS_CHAN_FLAG_ */

	/*
	 * RX bufring; immediately following ch_txbr.
	 */
	hv_vmbus_ring_buffer_info	ch_rxbr;

	struct taskqueue		*ch_tq;
	struct task			ch_task;
	vmbus_chan_callback_t		ch_cb;
	void				*ch_cbarg;

	/*
	 * TX bufring; at the beginning of ch_bufring.
	 *
	 * NOTE:
	 * Put TX bufring and the following MNF/evtflag to a new
	 * cacheline, since they will be accessed on all CPUs by
	 * locking ch_txbr first.
	 *
	 * XXX
	 * TX bufring and following MNF/evtflags do _not_ fit in
	 * one 64B cacheline.
	 */
	hv_vmbus_ring_buffer_info	ch_txbr __aligned(CACHE_LINE_SIZE);
	uint32_t			ch_txflags;	/* VMBUS_CHAN_TXF_ */

	/*
	 * These are based on the vmbus_chanmsg_choffer.chm_montrig.
	 * Save it here for easy access.
	 */
	uint32_t			ch_montrig_mask;/* MNF trig mask */
	volatile uint32_t		*ch_montrig;	/* MNF trigger loc. */

	/*
	 * These are based on the vmbus_chanmsg_choffer.chm_chanid.
	 * Save it here for easy access.
	 */
	u_long				ch_evtflag_mask;/* event flag */
	volatile u_long			*ch_evtflag;	/* event flag loc. */

	/*
	 * Rarely used fields.
	 */

	struct hyperv_mon_param		*ch_monprm;
	struct hyperv_dma		ch_monprm_dma;

	uint32_t			ch_id;		/* channel id */
	device_t			ch_dev;
	struct vmbus_softc		*ch_vmbus;

	int				ch_cpuid;	/* owner cpu */
	/*
	 * Virtual cpuid for ch_cpuid; it is used to communicate cpuid
	 * related information w/ Hyper-V.  If MSR_HV_VP_INDEX does not
	 * exist, ch_vcpuid will always be 0 for compatibility.
	 */
	uint32_t			ch_vcpuid;

	/*
	 * If this is a primary channel, ch_subchan* fields
	 * contain sub-channels belonging to this primary
	 * channel.
	 */
	struct mtx			ch_subchan_lock;
	TAILQ_HEAD(, vmbus_channel)	ch_subchans;
	int				ch_subchan_cnt;

	/* If this is a sub-channel */
	TAILQ_ENTRY(vmbus_channel)	ch_sublink;	/* sub-channel link */
	struct vmbus_channel		*ch_prichan;	/* owner primary chan */

	void				*ch_bufring;	/* TX+RX bufrings */
	struct hyperv_dma		ch_bufring_dma;
	uint32_t			ch_bufring_gpadl;

	struct task			ch_detach_task;
	TAILQ_ENTRY(vmbus_channel)	ch_prilink;	/* primary chan link */
	uint32_t			ch_subidx;	/* subchan index */
	volatile uint32_t		ch_stflags;	/* atomic-op */
							/* VMBUS_CHAN_ST_ */
	struct hyperv_guid		ch_guid_type;
	struct hyperv_guid		ch_guid_inst;

	struct sysctl_ctx_list		ch_sysctl_ctx;
} __aligned(CACHE_LINE_SIZE);

#define VMBUS_CHAN_ISPRIMARY(chan)	((chan)->ch_subidx == 0)

/*
 * If this flag is set, this channel's interrupt will be masked in ISR,
 * and the RX bufring will be drained before this channel's interrupt is
 * unmasked.
 *
 * This flag is turned on by default.  Drivers can turn it off according
 * to their own requirement.
 */
#define VMBUS_CHAN_FLAG_BATCHREAD	0x0002

#define VMBUS_CHAN_TXF_HASMNF		0x0001

#define VMBUS_CHAN_ST_OPENED_SHIFT	0
#define VMBUS_CHAN_ST_OPENED		(1 << VMBUS_CHAN_ST_OPENED_SHIFT)

struct vmbus_softc;
struct vmbus_message;

void	vmbus_event_proc(struct vmbus_softc *, int);
void	vmbus_event_proc_compat(struct vmbus_softc *, int);
void	vmbus_chan_msgproc(struct vmbus_softc *, const struct vmbus_message *);
void	vmbus_chan_destroy_all(struct vmbus_softc *);

#endif	/* !_VMBUS_CHANVAR_H_ */
