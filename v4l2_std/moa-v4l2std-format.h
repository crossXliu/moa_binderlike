#include <media/v4l2-common.h>
#define V4L2STD_PLANE_MAX 8

struct moa_v4l2std_fmt {
	u32 fourcc;
	int bpp;
	int buffers;
	int planes;
	int depth[V4L2STD_PLANE_MAX];
	struct v4l2_format vfmt;
	struct v4l2_format_info vinfo;
};

