#include <string.h>
#include "bitrate_ctrl.h"

// don't change
#define FIXED_POINT_SHIFT   8
#define IDR_FRAME           0
#define P_FRAME             1

// convergence speed adjustment
#define QP_SHIFT            (FIXED_POINT_SHIFT/2)
#define AVERAGE_STEP        8 // larger --> sensitivity lower (linear)

#if 0
static uint8_t HALFTONE_MATRIX[4][4] = {
    9, 192,  48, 240,
  128,  64, 176, 112,
   32, 224,  16, 208,
  160,  96, 144,  80
};
#endif


// exported APIs
void bitrate_ctrl_init(bitrate_ctrl_t *ctrl, int32_t target_bitrate, int32_t fps)
{
    memset(ctrl, 0, sizeof(bitrate_ctrl_t));
    ctrl->target_bitrate = target_bitrate;
    ctrl->target_frame_size = (target_bitrate << (FIXED_POINT_SHIFT-3)) / fps; // bytes
    ctrl->fps = fps;
    ctrl->min_qp = 26;
    ctrl->max_qp = 35;
    ctrl->max_qp_step = 4;
}

void bitrate_ctrl_setup_qp_range(bitrate_ctrl_t *ctrl, int32_t min_qp, int32_t max_qp)
{
    ctrl->min_qp = min_qp;
    ctrl->max_qp = max_qp;
}

static int int_sqrt(int x)
{
    if (x == 0 || x == 1)
        return x;
 
    int start = 1, end = x / 2, ans;
    while (start <= end) {
        int mid = (start + end) / 2;
 
        int sqr = mid * mid;
        if (sqr == x)
            return mid;
 
        if (sqr <= x) {
            start = mid + 1;
            ans = mid;
        }
        else
            end = mid - 1;
    }
    return ans;
}

#include <stdio.h>
int32_t bitrate_ctrl_update_frame(bitrate_ctrl_t *ctrl, uint8_t *frame, size_t size)
{
    int nal_type = frame[0] & 0x1f;
    if (nal_type == 5) { // IDR
        int size_diff =  (int)(size << FIXED_POINT_SHIFT) - ctrl->average_size; // keep the average size
        if (ctrl->gop > 0) {
            ctrl->i_cost = size_diff / ctrl->gop;
        } else {
            ctrl->i_cost = size_diff / 30; // assume gop 30 for the first IDR
        }
        ctrl->gop = 0;
    } else if (nal_type == 1) { // P
        ctrl->average_size = (ctrl->average_size * AVERAGE_STEP + ((size << FIXED_POINT_SHIFT) + ctrl->i_cost)) / (AVERAGE_STEP+1);
        ctrl->gop++;
    } else {
        return ctrl->cal_qp >> QP_SHIFT;
	}

    ctrl->frame_count++;
    // update the QP value
    int32_t ret_qp;
    int32_t slope, qp_step;
    if (ctrl->frame_count > AVERAGE_STEP) {
        if (ctrl->average_size > ctrl->target_frame_size) {
			//printf("average_size=%d, target=%d (ratio=%f)\n", ctrl->average_size, ctrl->target_frame_size, (float)ctrl->average_size / (float)ctrl->target_frame_size);
            slope = (ctrl->average_size << FIXED_POINT_SHIFT) / ctrl->target_frame_size;
            qp_step = int_sqrt(slope) / 9;
			//printf("qp_step(1)=%d\n", qp_step);
            ctrl->cal_qp += qp_step;
        } else {
			//printf("target=%d, average_size=%d (ratio=%f)\n", ctrl->target_frame_size, ctrl->average_size, (float)ctrl->target_frame_size / (float)ctrl->average_size);
            slope = (ctrl->target_frame_size << FIXED_POINT_SHIFT) / ctrl->average_size;
            qp_step = int_sqrt(slope) / 9;
			//printf("qp_step(2)=%d\n", qp_step);
            ctrl->cal_qp -= qp_step;
        }
		if (ctrl->cal_qp < (ctrl->min_qp << QP_SHIFT)) {
			ctrl->cal_qp = (ctrl->min_qp << QP_SHIFT);
		}
		if (ctrl->cal_qp > (ctrl->max_qp << QP_SHIFT)) {
			ctrl->cal_qp = (ctrl->max_qp << QP_SHIFT);
		}
        ret_qp =  ctrl->cal_qp >> QP_SHIFT;
    } else {
        ret_qp = (ctrl->min_qp + ctrl->max_qp)/2;
    }

	//printf("ctrl->cal_qp=%d\n", ctrl->cal_qp);
	// check boundary
	if (ret_qp < ctrl->min_qp) {
		ret_qp = ctrl->min_qp;
	} else if (ret_qp > ctrl->max_qp) {
		ret_qp = ctrl->max_qp;
	}

    return ret_qp;
}
