#pragma once

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitrate_ctrl_s {
    int32_t target_bitrate;
    int32_t min_qp;
    int32_t max_qp;
    int32_t cal_qp;
    int32_t average_size;
    int32_t target_frame_size;
    int32_t i_cost;
    int32_t frame_count;
    int32_t gop;
    int32_t fps;
    int32_t max_qp_step;
} bitrate_ctrl_t;

void bitrate_ctrl_init(bitrate_ctrl_t *ctrl, int32_t target_bitrate, int32_t fps);
void bitrate_ctrl_setup_qp_range(bitrate_ctrl_t *ctrl, int32_t min_qp, int32_t max_qp);
int32_t bitrate_ctrl_update_frame(bitrate_ctrl_t *ctrl, uint8_t *frame, size_t size);

#ifdef __cplusplus
};
#endif
