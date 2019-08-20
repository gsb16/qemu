/*
 * libqos virtio driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/virtio.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"

uint8_t qvirtio_config_readb(QVirtioDevice *d, uint64_t addr)
{
    return d->bus->config_readb(d, addr);
}

uint16_t qvirtio_config_readw(QVirtioDevice *d, uint64_t addr)
{
    return d->bus->config_readw(d, addr);
}

uint32_t qvirtio_config_readl(QVirtioDevice *d, uint64_t addr)
{
    return d->bus->config_readl(d, addr);
}

uint64_t qvirtio_config_readq(QVirtioDevice *d, uint64_t addr)
{
    return d->bus->config_readq(d, addr);
}

uint32_t qvirtio_get_features(QVirtioDevice *d)
{
    return d->bus->get_features(d);
}

void qvirtio_set_features(QVirtioDevice *d, uint32_t features)
{
    d->features = features;
    d->bus->set_features(d, features);
}

QVirtQueue *qvirtqueue_setup(QVirtioDevice *d,
                             QGuestAllocator *alloc, uint16_t index)
{
    return d->bus->virtqueue_setup(d, alloc, index);
}

void qvirtqueue_cleanup(const QVirtioBus *bus, QVirtQueue *vq,
                        QGuestAllocator *alloc)
{
    return bus->virtqueue_cleanup(vq, alloc);
}

void qvirtio_reset(QVirtioDevice *d)
{
    d->bus->set_status(d, 0);
    g_assert_cmphex(d->bus->get_status(d), ==, 0);
}

void qvirtio_set_acknowledge(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_ACKNOWLEDGE);
    g_assert_cmphex(d->bus->get_status(d), ==, VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

void qvirtio_set_driver(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_DRIVER);
    g_assert_cmphex(d->bus->get_status(d), ==,
                    VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

void qvirtio_set_driver_ok(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_DRIVER_OK);
    g_assert_cmphex(d->bus->get_status(d), ==, VIRTIO_CONFIG_S_DRIVER_OK |
                    VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

void qvirtio_wait_queue_isr(QVirtioDevice *d,
                            QVirtQueue *vq, gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    for (;;) {
        clock_step(100);
        if (d->bus->get_queue_isr_status(d, vq)) {
            return;
        }
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

/* Wait for the status byte at given guest memory address to be set
 *
 * The virtqueue interrupt must not be raised, making this useful for testing
 * event_index functionality.
 */
uint8_t qvirtio_wait_status_byte_no_isr(QTestState *qts, QVirtioDevice *d,
                                        QVirtQueue *vq,
                                        uint64_t addr,
                                        gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();
    uint8_t val;

    while ((val = readb(addr)) == 0xff) {
        clock_step(100);
        g_assert(!d->bus->get_queue_isr_status(d, vq));
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
    return val;
}

/*
 * qvirtio_wait_used_elem:
 * @desc_idx: The next expected vq->desc[] index in the used ring
 * @len: A pointer that is filled with the length written into the buffer, may
 *       be NULL
 * @timeout_us: How many microseconds to wait before failing
 *
 * This function waits for the next completed request on the used ring.
 */
void qvirtio_wait_used_elem(QTestState *qts, QVirtioDevice *d,
                            QVirtQueue *vq,
                            uint32_t desc_idx,
                            uint32_t *len,
                            gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    for (;;) {
        uint32_t got_desc_idx;

        clock_step(100);

        if (d->bus->get_queue_isr_status(d, vq) &&
            qvirtqueue_get_buf(qts, vq, &got_desc_idx, len)) {
            g_assert_cmpint(got_desc_idx, ==, desc_idx);
            return;
        }

        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

void qvirtio_wait_config_isr(QVirtioDevice *d, gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    for (;;) {
        clock_step(100);
        if (d->bus->get_config_isr_status(d)) {
            return;
        }
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

void qvring_init(QTestState *qts, const QGuestAllocator *alloc, QVirtQueue *vq,
                 uint64_t addr)
{
    int i;

    vq->desc = addr;
    vq->avail = vq->desc + vq->size * sizeof(struct vring_desc);
    vq->used = (uint64_t)((vq->avail + sizeof(uint16_t) * (3 + vq->size)
        + vq->align - 1) & ~(vq->align - 1));

    for (i = 0; i < vq->size - 1; i++) {
        /* vq->desc[i].addr */
        qtest_writeq(qts, vq->desc + (16 * i), 0);
        /* vq->desc[i].next */
        qtest_writew(qts, vq->desc + (16 * i) + 14, i + 1);
    }

    /* vq->avail->flags */
    qtest_writew(qts, vq->avail, 0);
    /* vq->avail->idx */
    qtest_writew(qts, vq->avail + 2, 0);
    /* vq->avail->used_event */
    qtest_writew(qts, vq->avail + 4 + (2 * vq->size), 0);

    /* vq->used->flags */
    qtest_writew(qts, vq->used, 0);
    /* vq->used->avail_event */
    qtest_writew(qts, vq->used + 2 + sizeof(struct vring_used_elem) * vq->size,
                 0);
}

QVRingIndirectDesc *qvring_indirect_desc_setup(QTestState *qs, QVirtioDevice *d,
                                               QGuestAllocator *alloc,
                                               uint16_t elem)
{
    int i;
    QVRingIndirectDesc *indirect = g_malloc(sizeof(*indirect));

    indirect->index = 0;
    indirect->elem = elem;
    indirect->desc = guest_alloc(alloc, sizeof(struct vring_desc) * elem);

    for (i = 0; i < elem - 1; ++i) {
        /* indirect->desc[i].addr */
        qtest_writeq(qs, indirect->desc + (16 * i), 0);
        /* indirect->desc[i].flags */
        qtest_writew(qs, indirect->desc + (16 * i) + 12, VRING_DESC_F_NEXT);
        /* indirect->desc[i].next */
        qtest_writew(qs, indirect->desc + (16 * i) + 14, i + 1);
    }

    return indirect;
}

void qvring_indirect_desc_add(QTestState *qts, QVRingIndirectDesc *indirect,
                              uint64_t data, uint32_t len, bool write)
{
    uint16_t flags;

    g_assert_cmpint(indirect->index, <, indirect->elem);

    flags = qtest_readw(qts, indirect->desc + (16 * indirect->index) + 12);

    if (write) {
        flags |= VRING_DESC_F_WRITE;
    }

    /* indirect->desc[indirect->index].addr */
    qtest_writeq(qts, indirect->desc + (16 * indirect->index), data);
    /* indirect->desc[indirect->index].len */
    qtest_writel(qts, indirect->desc + (16 * indirect->index) + 8, len);
    /* indirect->desc[indirect->index].flags */
    qtest_writew(qts, indirect->desc + (16 * indirect->index) + 12, flags);

    indirect->index++;
}

uint32_t qvirtqueue_add(QTestState *qts, QVirtQueue *vq, uint64_t data,
                        uint32_t len, bool write, bool next)
{
    uint16_t flags = 0;
    vq->num_free--;

    if (write) {
        flags |= VRING_DESC_F_WRITE;
    }

    if (next) {
        flags |= VRING_DESC_F_NEXT;
    }

    /* vq->desc[vq->free_head].addr */
    qtest_writeq(qts, vq->desc + (16 * vq->free_head), data);
    /* vq->desc[vq->free_head].len */
    qtest_writel(qts, vq->desc + (16 * vq->free_head) + 8, len);
    /* vq->desc[vq->free_head].flags */
    qtest_writew(qts, vq->desc + (16 * vq->free_head) + 12, flags);

    return vq->free_head++; /* Return and increase, in this order */
}

uint32_t qvirtqueue_add_indirect(QTestState *qts, QVirtQueue *vq,
                                 QVRingIndirectDesc *indirect)
{
    g_assert(vq->indirect);
    g_assert_cmpint(vq->size, >=, indirect->elem);
    g_assert_cmpint(indirect->index, ==, indirect->elem);

    vq->num_free--;

    /* vq->desc[vq->free_head].addr */
    qtest_writeq(qts, vq->desc + (16 * vq->free_head), indirect->desc);
    /* vq->desc[vq->free_head].len */
    qtest_writel(qts, vq->desc + (16 * vq->free_head) + 8,
           sizeof(struct vring_desc) * indirect->elem);
    /* vq->desc[vq->free_head].flags */
    qtest_writew(qts, vq->desc + (16 * vq->free_head) + 12,
                 VRING_DESC_F_INDIRECT);

    return vq->free_head++; /* Return and increase, in this order */
}

void qvirtqueue_kick(QTestState *qts, QVirtioDevice *d, QVirtQueue *vq,
                     uint32_t free_head)
{
    /* vq->avail->idx */
    uint16_t idx = qtest_readw(qts, vq->avail + 2);
    /* vq->used->flags */
    uint16_t flags;
    /* vq->used->avail_event */
    uint16_t avail_event;

    /* vq->avail->ring[idx % vq->size] */
    qtest_writew(qts, vq->avail + 4 + (2 * (idx % vq->size)), free_head);
    /* vq->avail->idx */
    qtest_writew(qts, vq->avail + 2, idx + 1);

    /* Must read after idx is updated */
    flags = qtest_readw(qts, vq->avail);
    avail_event = qtest_readw(qts, vq->used + 4 +
                                   sizeof(struct vring_used_elem) * vq->size);

    /* < 1 because we add elements to avail queue one by one */
    if ((flags & VRING_USED_F_NO_NOTIFY) == 0 &&
                            (!vq->event || (uint16_t)(idx-avail_event) < 1)) {
        d->bus->virtqueue_kick(d, vq);
    }
}

/*
 * qvirtqueue_get_buf:
 * @desc_idx: A pointer that is filled with the vq->desc[] index, may be NULL
 * @len: A pointer that is filled with the length written into the buffer, may
 *       be NULL
 *
 * This function gets the next used element if there is one ready.
 *
 * Returns: true if an element was ready, false otherwise
 */
bool qvirtqueue_get_buf(QTestState *qts, QVirtQueue *vq, uint32_t *desc_idx,
                        uint32_t *len)
{
    uint16_t idx;
    uint64_t elem_addr, addr;

    idx = qtest_readw(qts, vq->used + offsetof(struct vring_used, idx));
    if (idx == vq->last_used_idx) {
        return false;
    }

    elem_addr = vq->used +
        offsetof(struct vring_used, ring) +
        (vq->last_used_idx % vq->size) *
        sizeof(struct vring_used_elem);

    if (desc_idx) {
        addr = elem_addr + offsetof(struct vring_used_elem, id);
        *desc_idx = qtest_readl(qts, addr);
    }

    if (len) {
        addr = elem_addr + offsetof(struct vring_used_elem, len);
        *len = qtest_readw(qts, addr);
    }

    vq->last_used_idx++;
    return true;
}

void qvirtqueue_set_used_event(QTestState *qts, QVirtQueue *vq, uint16_t idx)
{
    g_assert(vq->event);

    /* vq->avail->used_event */
    qtest_writew(qts, vq->avail + 4 + (2 * vq->size), idx);
}

void qvirtio_start_device(QVirtioDevice *vdev)
{
    qvirtio_reset(vdev);
    qvirtio_set_acknowledge(vdev);
    qvirtio_set_driver(vdev);
}

bool qvirtio_is_big_endian(QVirtioDevice *d)
{
    return d->big_endian;
}
