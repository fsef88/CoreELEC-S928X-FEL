/*
 * test_dvfel_queue.c - Unit test of dvfel kernel ring buffer (EL_HOLD = 16)
 *                     and B-pyramid STDL2 frame synchronization logic
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define DRV_NAME "dvfel_test"
#define EL_HOLD_NEW 16
#define EL_HOLD_OLD 8
#define EL_STALE 16

#define DVFEL_DONE_COMPOSED    0
#define DVFEL_DONE_PASSTHROUGH 1

#define PAIR(id) ((id) >> 16)

struct mock_vframe {
    uint32_t frame_index;
    uint64_t pts_us64;
};

struct mock_dvfel_dev {
    int el_hold_capacity;
    struct mock_vframe *el_hold[EL_HOLD_NEW + 1];
    int el_nhold;
    int el_in;
    int el_dropped;
    int el_missing;
    int el_used;
    int el_wait_ms;
};

static struct mock_vframe *el_hold_remove(struct mock_dvfel_dev *d, int i) {
    struct mock_vframe *el = d->el_hold[i];
    memmove(&d->el_hold[i], &d->el_hold[i + 1], (d->el_nhold - i - 1) * sizeof(el));
    d->el_nhold--;
    return el;
}

static struct mock_vframe *simulate_el_take(struct mock_dvfel_dev *d, struct mock_vframe *bl, bool simulate_wait_timeout) {
    struct mock_vframe *drop[EL_HOLD_NEW + 1], *el = NULL;
    uint32_t id = bl->frame_index;
    int i, ndrop = 0;

    /* Match frame by ID pairing */
    for (i = 0; i < d->el_nhold && !el; i++) {
        if (id && PAIR(d->el_hold[i]->frame_index) == PAIR(id)) {
            el = el_hold_remove(d, i);
        }
    }

    /* Stale ones */
    for (i = 0; i < d->el_nhold;) {
        if (id && (int16_t)(PAIR(d->el_hold[i]->frame_index) - PAIR(id)) < -EL_STALE) {
            drop[ndrop++] = el_hold_remove(d, i);
        } else {
            i++;
        }
    }

    /* When hold is strictly full and no match yet */
    if (!el && d->el_nhold == d->el_hold_capacity) {
        drop[ndrop++] = el_hold_remove(d, 0);
    }

    for (i = 0; i < ndrop; i++) {
        d->el_dropped++;
    }

    if (simulate_wait_timeout && !el) {
        /* Timeout occurred before EL was available */
        d->el_missing++;
        return NULL;
    }

    if (el) {
        d->el_used++;
    } else {
        d->el_missing++;
    }
    return el;
}

static void push_el_frame(struct mock_dvfel_dev *d, struct mock_vframe *v) {
    if (d->el_nhold < d->el_hold_capacity) {
        d->el_hold[d->el_nhold++] = v;
        d->el_in++;
    } else {
        /* Buffer full */
        struct mock_vframe *dropped = el_hold_remove(d, 0);
        d->el_dropped++;
        d->el_hold[d->el_nhold++] = v;
        d->el_in++;
    }
}

int main() {
    printf("=================================================================\n");
    printf("  dvfel Ring Buffer Synchronization & STDL2 Deep B-Pyramid Test\n");
    printf("=================================================================\n");

    /*
     * Scenario: HEVC Profile 7 STDL2 with deep B-pyramid (depth = 14 frames).
     * The EL decoder produces 14 frames ahead of the BL display thread.
     */
    const int TOTAL_FRAMES = 60;
    const int LEAD_FRAMES = 14;

    struct mock_vframe bl_frames[TOTAL_FRAMES];
    struct mock_vframe el_frames[TOTAL_FRAMES];

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        uint32_t pair_id = (uint32_t)((i + 1) << 16);
        bl_frames[i].frame_index = pair_id | 1;
        bl_frames[i].pts_us64 = (uint64_t)i * 41708ULL;

        el_frames[i].frame_index = pair_id | 2;
        el_frames[i].pts_us64 = (uint64_t)i * 41708ULL;
    }

    printf("\n[1] Testing OLD Driver Settings: EL_HOLD = 8, el_wait_ms = 20...\n");
    struct mock_dvfel_dev dev_old;
    memset(&dev_old, 0, sizeof(dev_old));
    dev_old.el_hold_capacity = EL_HOLD_OLD;
    dev_old.el_wait_ms = 20;

    int old_passthrough_count = 0;
    int el_pushed = 0;

    /* Push leading EL frames */
    for (int i = 0; i < LEAD_FRAMES && i < TOTAL_FRAMES; i++) {
        push_el_frame(&dev_old, &el_frames[i]);
        el_pushed++;
    }

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (el_pushed < TOTAL_FRAMES) {
            push_el_frame(&dev_old, &el_frames[el_pushed++]);
        }
        struct mock_vframe *el = simulate_el_take(&dev_old, &bl_frames[i], false);
        int job_status = (el != NULL) ? DVFEL_DONE_COMPOSED : DVFEL_DONE_PASSTHROUGH;
        if (job_status == DVFEL_DONE_PASSTHROUGH) {
            old_passthrough_count++;
        }
    }

    printf("  -> Result (OLD): Dropped ELs: %d | Missing ELs: %d | Fallbacks (Flashes): %d\n",
           dev_old.el_dropped, dev_old.el_missing, old_passthrough_count);
    if (old_passthrough_count > 0) {
        printf("  -> CONFIRMED: Old setting causes %d frame drop flashes on deep B-pyramids!\n", old_passthrough_count);
    }

    printf("\n[2] Testing NEW Driver Settings: EL_HOLD = 16, el_wait_ms = 80...\n");
    struct mock_dvfel_dev dev_new;
    memset(&dev_new, 0, sizeof(dev_new));
    dev_new.el_hold_capacity = EL_HOLD_NEW;
    dev_new.el_wait_ms = 80;

    int new_passthrough_count = 0;
    el_pushed = 0;

    /* Push leading EL frames */
    for (int i = 0; i < LEAD_FRAMES && i < TOTAL_FRAMES; i++) {
        push_el_frame(&dev_new, &el_frames[i]);
        el_pushed++;
    }

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (el_pushed < TOTAL_FRAMES) {
            push_el_frame(&dev_new, &el_frames[el_pushed++]);
        }
        struct mock_vframe *el = simulate_el_take(&dev_new, &bl_frames[i], false);
        int job_status = (el != NULL) ? DVFEL_DONE_COMPOSED : DVFEL_DONE_PASSTHROUGH;
        if (job_status == DVFEL_DONE_PASSTHROUGH) {
            new_passthrough_count++;
        }
    }

    printf("  -> Result (NEW): Dropped ELs: %d | Missing ELs: %d | Fallbacks (Flashes): %d\n",
           dev_new.el_dropped, dev_new.el_missing, new_passthrough_count);

    /* [3] Test 16-bit access unit wrap-around */
    printf("\n[3] Testing 16-bit AU Wrap-around Arithmetic (65530 -> 10)...\n");
    uint16_t bl_au = 65530;
    uint16_t el_au = 6;
    int16_t au_delta = (int16_t)(el_au - bl_au);
    printf("  -> bl_au = %u, el_au = %u => au_delta_s16 = %d (Expected: +12)\n",
           bl_au, el_au, (int)au_delta);

    int test3_ok = (dev_new.el_dropped == 0) &&
                   (dev_new.el_missing == 0) &&
                   (new_passthrough_count == 0) &&
                   (au_delta == 12);

    printf("\n=================================================================\n");
    if (test3_ok) {
        printf(">>> TEST 3 RESULT: QUEUE SYNCHRONIZATION TEST PASSED (PASS) <<<\n");
    } else {
        printf(">>> TEST 3 RESULT: QUEUE SYNCHRONIZATION TEST FAILED (FAIL) <<<\n");
    }
    printf("=================================================================\n");

    return test3_ok ? 0 : 1;
}
