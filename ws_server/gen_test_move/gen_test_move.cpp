#include <stdio.h>
#include <string.h>

const char *format = "{\"type\": \"object_detection\",\"result\": {\"status\":\"ok\",\"elapsed_time\":1817,\"model\":\"yolov4-416-fp32-3\",\"width\":640,\"height\":360,\"score\":60,\"iou\":60,\"detection\":[{\"minx\":%d,\"maxx\":%d,\"miny\":%d,\"maxy\":%d,\"score\":99,\"id\":0,\"class\":\"%s\"}]}}";

#define BLOCKSIZE   50
int main(int argc, char **argv)
{
    char buf[1024];
	char className[256];
    int minx, maxx, miny, maxy;

    miny = 180-BLOCKSIZE;
    maxy = 180+BLOCKSIZE;
    for (int minx=0; minx<640; minx++) {
		//sprintf(className, "%s%d", "person", (int)(minx/20));
		strcpy(className, "person");
        maxx = minx + BLOCKSIZE;
        sprintf(buf, format, minx, maxx, miny, maxy, className);
        printf("%s\n", buf);
    }
}

